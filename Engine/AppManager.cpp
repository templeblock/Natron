/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "AppManager.h"
#include "AppManagerPrivate.h"

#include <clocale>
#include <csignal>
#include <cstddef>
#include <cassert>
#include <stdexcept>
#include <cstring> // for std::memcpy

#if defined(Q_OS_LINUX)
#include <sys/signal.h>
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <ucontext.h>
#include <execinfo.h>
#endif

#ifdef Q_OS_UNIX
#include <stdio.h>
#include <stdlib.h>
#ifdef Q_OS_MAC
#include <sys/sysctl.h>
#include <libproc.h>
#endif
#endif

#ifdef Q_OS_WIN
#include <shlobj.h>
#endif

#include <boost/algorithm/string.hpp>
#include <boost/version.hpp>
#include <libs/hoedown/src/version.h>
#include <ceres/version.h>
#include <openMVG/version.hpp>

#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QTextCodec>
#include <QtCore/QCoreApplication>
#include <QtCore/QSettings>
#include <QtCore/QThreadPool>
#include <QtCore/QTextStream>
#include <QtNetwork/QAbstractSocket>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>


#include "Global/ProcInfo.h"
#include "Global/GLIncludes.h"
#include "Global/QtCompat.h"
#include "Global/StrUtils.h"

#include "Engine/AppInstance.h"
#include "Engine/Backdrop.h"
#include "Engine/CLArgs.h"
#include "Engine/CreateNodeArgs.h"
#include "Engine/DiskCacheNode.h"
#include "Engine/DimensionIdx.h"
#include "Engine/Dot.h"
#include "Engine/ExistenceCheckThread.h"
#include "Engine/FStreamsSupport.h"
#include "Engine/GroupInput.h"
#include "Engine/GroupOutput.h"
#include "Engine/LibraryBinary.h"
#include "Engine/Log.h"
#include "Engine/Node.h"
#include "Engine/FileSystemModel.h"
#include "Engine/JoinViewsNode.h"
#include "Engine/OfxImageEffectInstance.h"
#include "Engine/OfxEffectInstance.h"
#include "Engine/OfxHost.h"
#include "Engine/OSGLContext.h"
#include "Engine/OSGLFunctions.h"
#include "Engine/OneViewNode.h"
#include "Engine/ProcessHandler.h" // ProcessInputChannel
#include "Engine/Project.h"
#include "Engine/PrecompNode.h"
#include "Engine/ReadNode.h"
#include "Engine/RotoPaint.h"
#include "Engine/RotoShapeRenderNode.h"
#include "Engine/RotoShapeRenderCairo.h"
#include "Engine/StandardPaths.h"
#include "Engine/StubNode.h"
#include "Engine/TrackerNode.h"
#include "Engine/ThreadPool.h"
#include "Engine/ViewIdx.h"
#include "Engine/ViewerInstance.h" // RenderStatsMap
#include "Engine/ViewerNode.h"
#include "Engine/WriteNode.h"

#include "Serialization/NodeSerialization.h"
#include "Serialization/SerializationIO.h"

#include "sbkversion.h" // shiboken/pyside version

#if QT_VERSION < 0x050000
Q_DECLARE_METATYPE(QAbstractSocket::SocketState)
#endif

NATRON_NAMESPACE_ENTER;

AppManager* AppManager::_instance = 0;

#ifdef __NATRON_UNIX__

//namespace  {
static void
handleShutDownSignal( int /*signalId*/ )
{
    if (appPTR) {
        std::cerr << "\nCaught termination signal, exiting!" << std::endl;
        appPTR->quitApplication();
    }
}

static void
setShutDownSignal(int signalId)
{
#if defined(__NATRON_UNIX__)
    struct sigaction sa;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = handleShutDownSignal;
    if (sigaction(signalId, &sa, NULL) == -1) {
        std::perror("setting up termination signal");
        std::exit(1);
    }
#else
    std::signal(signalId, handleShutDownSignal);
#endif
}

#endif


#if defined(__NATRON_LINUX__) && !defined(__FreeBSD__)

#define NATRON_UNIX_BACKTRACE_STACK_DEPTH 16

static void
backTraceSigSegvHandler(int sig,
                        siginfo_t *info,
                        void *secret)
{
    void *trace[NATRON_UNIX_BACKTRACE_STACK_DEPTH];
    char **messages = (char **)NULL;
    int i, trace_size = 0;
    ucontext_t *uc = (ucontext_t *)secret;

    /* Do something useful with siginfo_t */
    if (sig == SIGSEGV) {
        QThread* curThread = QThread::currentThread();
        std::string threadName;
        if (curThread) {
            threadName = (qApp && qApp->thread() == curThread) ? "Main" : curThread->objectName().toStdString();
        }
        std::cerr << "Caught segmentation fault (SIGSEGV) from thread "  << threadName << "(" << curThread << "), faulty address is " <<
             #ifndef __x86_64__
        (void*)uc->uc_mcontext.gregs[REG_EIP]
             #else
            (void*) uc->uc_mcontext.gregs[REG_RIP]
             #endif
            << " from " << info->si_addr << std::endl;
    } else {
        printf("Got signal %d#92;n", sig);
    }

    trace_size = backtrace(trace, NATRON_UNIX_BACKTRACE_STACK_DEPTH);
    /* overwrite sigaction with caller's address */
#ifndef __x86_64__
    trace[1] = (void *) uc->uc_mcontext.gregs[REG_EIP];
#else
    trace[1] = (void *) uc->uc_mcontext.gregs[REG_RIP];
#endif


    messages = backtrace_symbols(trace, trace_size);
    /* skip first stack frame (points here) */
    std::cerr << "Backtrace:" << std::endl;
    for (i = 1; i < trace_size; ++i) {
        std::cerr << "[Frame " << i << "]: " << messages[i] << std::endl;
    }
    exit(1);
}

static void
setSigSegvSignal()
{
    struct sigaction sa;

    sigemptyset (&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    /* if SA_SIGINFO is set, sa_sigaction is to be used instead of sa_handler. */
    sa.sa_sigaction = backTraceSigSegvHandler;

    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        std::perror("setting up sigsegv signal");
        std::exit(1);
    }
}

#endif // if defined(__NATRON_LINUX__) && !defined(__FreeBSD__)


#if PY_MAJOR_VERSION >= 3
// Python 3

//Borrowed from https://github.com/python/cpython/blob/634cb7aa2936a09e84c5787d311291f0e042dba3/Python/fileutils.c
//Somehow Python 3 dev forced every C application embedding python to have their own code to convert char** to wchar_t**
static wchar_t*
char2wchar(char* arg)
{
    wchar_t *res = NULL;

#ifdef HAVE_BROKEN_MBSTOWCS
    /* Some platforms have a broken implementation of
     * mbstowcs which does not count the characters that
     * would result from conversion.  Use an upper bound.
     */
    size_t argsize = strlen(arg);
#else
    size_t argsize = mbstowcs(NULL, arg, 0);
#endif
    size_t count;
    unsigned char *in;
    wchar_t *out;
#ifdef HAVE_MBRTOWC
    mbstate_t mbs;
#endif
    if (argsize != (size_t)-1) {
        res = (wchar_t *)malloc( (argsize + 1) * sizeof(wchar_t) );
        if (!res) {
            goto oom;
        }
        count = mbstowcs(res, arg, argsize + 1);
        if (count != (size_t)-1) {
            wchar_t *tmp;
            /* Only use the result if it contains no
             surrogate characters. */
            for (tmp = res; *tmp != 0 &&
                 (*tmp < 0xd800 || *tmp > 0xdfff); tmp++) {
                ;
            }
            if (*tmp == 0) {
                return res;
            }
        }
        free(res);
    }
    /* Conversion failed. Fall back to escaping with surrogateescape. */
#ifdef HAVE_MBRTOWC
    /* Try conversion with mbrtwoc (C99), and escape non-decodable bytes. */
    /* Overallocate; as multi-byte characters are in the argument, the
     actual output could use less memory. */
    argsize = strlen(arg) + 1;
    res = (wchar_t*)malloc( argsize * sizeof(wchar_t) );
    if (!res) {
        goto oom;
    }
    in = (unsigned char*)arg;
    out = res;
    std::memset(&mbs, 0, sizeof mbs);
    while (argsize) {
        size_t converted = mbrtowc(out, (char*)in, argsize, &mbs);
        if (converted == 0) {
            /* Reached end of string; null char stored. */
            break;
        }
        if (converted == (size_t)-2) {
            /* Incomplete character. This should never happen,
             since we provide everything that we have -
             unless there is a bug in the C library, or I
             misunderstood how mbrtowc works. */
            fprintf(stderr, "unexpected mbrtowc result -2\n");
            free(res);

            return NULL;
        }
        if (converted == (size_t)-1) {
            /* Conversion error. Escape as UTF-8b, and start over
             in the initial shift state. */
            *out++ = 0xdc00 + *in++;
            argsize--;
            std::memset(&mbs, 0, sizeof mbs);
            continue;
        }
        if ( (*out >= 0xd800) && (*out <= 0xdfff) ) {
            /* Surrogate character.  Escape the original
             byte sequence with surrogateescape. */
            argsize -= converted;
            while (converted--) {
                *out++ = 0xdc00 + *in++;
            }
            continue;
        }
        /* successfully converted some bytes */
        in += converted;
        argsize -= converted;
        out++;
    }
#else
    /* Cannot use C locale for escaping; manually escape as if charset
     is ASCII (i.e. escape all bytes > 128. This will still roundtrip
     correctly in the locale's charset, which must be an ASCII superset. */
    res = (wchar_t*)malloc( (strlen(arg) + 1) * sizeof(wchar_t) );
    if (!res) {
        goto oom;
    }
    in = (unsigned char*)arg;
    out = res;
    while (*in) {
        if (*in < 128) {
            *out++ = *in++;
        } else {
            *out++ = 0xdc00 + *in++;
        }
    }
    *out = 0;
#endif // ifdef HAVE_MBRTOWC

    return res;
oom:
    fprintf(stderr, "out of memory\n");
    free(res);

    return NULL;
} // char2wchar

#endif // if PY_MAJOR_VERSION >= 3


//} // anon namespace

void
AppManager::saveCaches() const
{
    _imp->saveCaches();
}

int
AppManager::getHardwareIdealThreadCount()
{
    return _imp->idealThreadCount;
}

AppManager::AppManager()
    : QObject()
    , _imp( new AppManagerPrivate() )
{
    assert(!_instance);
    _instance = this;

    QObject::connect( this, SIGNAL(s_requestOFXDialogOnMainThread(OfxImageEffectInstance*,void*)), this, SLOT(onOFXDialogOnMainThreadReceived(OfxImageEffectInstance*,void*)) );

#ifdef __NATRON_WIN32__
    FileSystemModel::initDriveLettersToNetworkShareNamesMapping();
#endif
}

void
AppManager::takeNatronGIL()
{
    _imp->natronPythonGIL.lock();
}

void
AppManager::releaseNatronGIL()
{
    _imp->natronPythonGIL.unlock();
}

void
AppManager::loadProjectFromFileFunction(std::istream& ifile, const std::string& filename, const AppInstancePtr& /*app*/, SERIALIZATION_NAMESPACE::ProjectSerialization* obj)
{
    if (!SERIALIZATION_NAMESPACE::read(NATRON_PROJECT_FILE_HEADER,  ifile, obj)) {
        throw std::runtime_error(tr("Failed to open %1: This file does not appear to be a %2 project file").arg(QString::fromUtf8(filename.c_str())).arg(QString::fromUtf8(NATRON_APPLICATION_NAME)).toStdString());
    }

}

bool
AppManager::checkForOlderProjectFile(const AppInstancePtr& app, const QString& filePathIn, QString* filePathOut)
{
    *filePathOut = filePathIn;

    FStreamsSupport::ifstream ifile;
    FStreamsSupport::open( &ifile, filePathIn.toStdString() );
    if (!ifile) {
        throw std::runtime_error( tr("Failed to open %1").arg(filePathIn).toStdString() );
    }

    {
        // Try to determine if this is a project made with Natron > 2.2 or an older project
        std::string firstLine;
        std::getline(ifile, firstLine);
        if (firstLine.find("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>") != std::string::npos) {
            // This is an old boost serialization file, convert the project first
            QString path = appPTR->getApplicationBinaryPath();
            StrUtils::ensureLastPathSeparator(path);
            path += QString::fromUtf8("NatronProjectConverter");

#ifdef __NATRON_WIN32__
            path += QString::fromUtf8(".exe");
#endif

            if (!QFile::exists(path)) {
                throw std::runtime_error( tr("Could not find executable %1").arg(path).toStdString() );
            }

            app->updateProjectLoadStatus(tr("Converting project to newer format"));

            QString baseNameIn;
            {
                int foundLastDot = filePathIn.lastIndexOf(QLatin1Char('/'));
                if (foundLastDot != -1) {
                    baseNameIn = filePathIn.mid(foundLastDot + 1);
                }
            }

            filePathOut->clear();
            filePathOut->append(StandardPaths::writableLocation(StandardPaths::eStandardLocationTemp));
            StrUtils::ensureLastPathSeparator(*filePathOut);
            filePathOut->append( QString::number( QDateTime::currentDateTime().toMSecsSinceEpoch() ) );
            filePathOut->append(baseNameIn);

            QProcess proc;

            QStringList args;
            args << QString::fromUtf8("-i") << filePathIn << QString::fromUtf8("-o") << *filePathOut;
            proc.start(path, args);
            proc.waitForFinished();
            if (proc.exitCode() == 0 && proc.exitStatus() == QProcess::NormalExit) {
                return true;
            } else {
                QString error = QString::fromUtf8(proc.readAllStandardError().data());
                throw std::runtime_error(error.toStdString());
            }
        }
    }
    return false;
}

bool
AppManager::loadFromArgs(const CLArgs& cl)
{


    // Ensure Qt knows C-strings are UTF-8 before creating the QApplication for argv
#if QT_VERSION < 0x050000
    // be forward compatible: source code is UTF-8, and Qt5 assumes UTF-8 by default
    QTextCodec::setCodecForCStrings( QTextCodec::codecForName("UTF-8") );
    QTextCodec::setCodecForTr( QTextCodec::codecForName("UTF-8") );
#endif


    // This needs to be done BEFORE creating qApp because
    // on Linux, X11 will create a context that would corrupt
    // the XUniqueContext created by Qt
    _imp->renderingContextPool.reset( new GPUContextPool() );
    initializeOpenGLFunctionsOnce(true);

    //  QCoreApplication will hold a reference to that appManagerArgc integer until it dies.
    //  Thus ensure that the QCoreApplication is destroyed when returning this function.
    initializeQApp(_imp->nArgs, &_imp->commandLineArgsUtf8.front());
    // see C++ standard 23.2.4.2 vector capacity [lib.vector.capacity]
    // resizing to a smaller size doesn't free/move memory, so the data pointer remains valid
    assert(_imp->nArgs <= (int)_imp->commandLineArgsUtf8.size());
    _imp->commandLineArgsUtf8.resize(_imp->nArgs); // Qt may have reduced the numlber of args

#ifdef QT_CUSTOM_THREADPOOL
    // Set the global thread pool
    QThreadPool::setGlobalInstance(new ThreadPool);
#endif

    // set fontconfig path on all platforms
    if ( qgetenv("FONTCONFIG_PATH").isNull() ) {
        // set FONTCONFIG_PATH to Natron/Resources/etc/fonts (required by plugins using fontconfig)
        QString path = QCoreApplication::applicationDirPath() + QString::fromUtf8("/../Resources/etc/fonts");
        QFileInfo fileInfo(path);
        if ( !fileInfo.exists() ) {
            std::cerr <<  "Fontconfig configuration file " << fileInfo.canonicalFilePath().toStdString() << " does not exist, not setting FONTCONFIG_PATH "<< std::endl;
        } else {
            QString fcPath = fileInfo.canonicalFilePath();

            std::string stdFcPath = fcPath.toStdString();

            // qputenv on minw will just call putenv, but we want to keep the utf16 info, so we need to call _wputenv
            qDebug() << "Setting FONTCONFIG_PATH to" << stdFcPath.c_str();
#if 0 //def __NATRON_WIN32__
            _wputenv_s(L"FONTCONFIG_PATH", StrUtils::utf8_to_utf16(stdFcPath).c_str());
#else
             qputenv( "FONTCONFIG_PATH", stdFcPath.c_str() );
#endif
        }
    }

    try {
        initPython();
    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << std::endl;

        return false;
    }

    _imp->idealThreadCount = QThread::idealThreadCount();


    QThreadPool::globalInstance()->setExpiryTimeout(-1); //< make threads never exit on their own
    //otherwise it might crash with thread local storage


    ///the QCoreApplication must have been created so far.
    assert(qApp);

    bool ret = false;
    try {
        ret = loadInternal(cl);
    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << std::endl;
    }
    return ret;
} // loadFromArgs

bool
AppManager::load(int argc,
                 char **argv,
                 const CLArgs& cl)
{
    // Ensure application has correct locale before doing anything
    // Warning: Qt resets it in the QCoreApplication constructor
    // see http://doc.qt.io/qt-4.8/qcoreapplication.html#locale-settings
    setApplicationLocale();
    setApplicationLocale();
    _imp->handleCommandLineArgs(argc, argv);
    return loadFromArgs(cl);
}

bool
AppManager::loadW(int argc,
                 wchar_t **argv,
                 const CLArgs& cl)
{
    // Ensure application has correct locale before doing anything
    // Warning: Qt resets it in the QCoreApplication constructor
    // see http://doc.qt.io/qt-4.8/qcoreapplication.html#locale-settings
    setApplicationLocale();
    _imp->handleCommandLineArgsW(argc, argv);
    return loadFromArgs(cl);
}

AppManager::~AppManager()
{
#ifdef NATRON_USE_BREAKPAD
    if (_imp->breakpadAliveThread) {
        _imp->breakpadAliveThread->quitThread();
    }
#endif

    bool appsEmpty;
    {
        QMutexLocker k(&_imp->_appInstancesMutex);
        appsEmpty = _imp->_appInstances.empty();
    }
    while (!appsEmpty) {
        AppInstancePtr front;
        {
            QMutexLocker k(&_imp->_appInstancesMutex);
            front = _imp->_appInstances.front();
        }
        if (front) {
            front->quitNow();
        }
        {
            QMutexLocker k(&_imp->_appInstancesMutex);
            appsEmpty = _imp->_appInstances.empty();
        }
    }


    _imp->_backgroundIPC.reset();

    try {
        _imp->saveCaches();
    } catch (std::runtime_error) {
        // ignore errors
    }

    ///Caches may have launched some threads to delete images, wait for them to be done
    QThreadPool::globalInstance()->waitForDone();

    ///Kill caches now because decreaseNCacheFilesOpened can be called
    _imp->_nodeCache->waitForDeleterThread();
    _imp->_diskCache->waitForDeleterThread();
    _imp->_viewerCache->waitForDeleterThread();
    _imp->_nodeCache.reset();
    _imp->_viewerCache.reset();
    _imp->_diskCache.reset();

    tearDownPython();
    _imp->tearDownGL();

    _instance = 0;

    // After this line, everything is cleaned-up (should be) and the process may resume in the main and could in theory be able to re-create a new AppManager
    _imp->_qApp.reset();
}

class QuitInstanceArgs
    : public GenericWatcherCallerArgs
{
public:

    AppInstanceWPtr instance;

    QuitInstanceArgs()
        : GenericWatcherCallerArgs()
        , instance()
    {
    }

    virtual ~QuitInstanceArgs() {}
};

void
AppManager::afterQuitProcessingCallback(const WatcherCallerArgsPtr& args)
{
    QuitInstanceArgs* inArgs = dynamic_cast<QuitInstanceArgs*>( args.get() );

    if (!inArgs) {
        return;
    }

    AppInstancePtr instance = inArgs->instance.lock();

    instance->aboutToQuit();

    appPTR->removeInstance( instance->getAppID() );

    int nbApps = getNumInstances();
    ///if we exited the last instance, exit the event loop, this will make
    /// the exec() function return.
    if (nbApps == 0) {
        assert(qApp);
        qApp->quit();
    }

    // This should kill the AppInstance
    instance.reset();
}

void
AppManager::quitNow(const AppInstancePtr& instance)
{
    NodesList nodesToWatch;

    instance->getProject()->getNodes_recursive(nodesToWatch, false);
    if ( !nodesToWatch.empty() ) {
        for (NodesList::iterator it = nodesToWatch.begin(); it != nodesToWatch.end(); ++it) {
            (*it)->quitAnyProcessing_blocking(false);
        }
    }
    boost::shared_ptr<QuitInstanceArgs> args(new QuitInstanceArgs);
    args->instance = instance;
    afterQuitProcessingCallback(args);
}

void
AppManager::quit(const AppInstancePtr& instance)
{
    boost::shared_ptr<QuitInstanceArgs> args(new QuitInstanceArgs);

    args->instance = instance;
    if ( !instance->getProject()->quitAnyProcessingForAllNodes(this, args) ) {
        afterQuitProcessingCallback(args);
    }
}

void
AppManager::quitApplication()
{
    bool appsEmpty;
    {
        QMutexLocker k(&_imp->_appInstancesMutex);
        appsEmpty = _imp->_appInstances.empty();
    }

    while (!appsEmpty) {
        AppInstancePtr app;
        {
            QMutexLocker k(&_imp->_appInstancesMutex);
            app = _imp->_appInstances.front();
        }
        if (app) {
            quitNow(app);
        }

        {
            QMutexLocker k(&_imp->_appInstancesMutex);
            appsEmpty = _imp->_appInstances.empty();
        }
    }
}

void
AppManager::initializeQApp(int &argc,
                           char **argv)
{
    assert(!_imp->_qApp);
    _imp->_qApp.reset( new QCoreApplication(argc, argv) );
}

// setApplicationLocale is called twice:
// - before parsing the command-line arguments
// - after the QCoreApplication was constructed, because the QCoreApplication
// constructor resets the locale to the system locale
// see http://doc.qt.io/qt-4.8/qcoreapplication.html#locale-settings
void
AppManager::setApplicationLocale()
{
    // Natron is not yet internationalized, so it is better for now to use the "C" locale,
    // until it is tested for robustness against locale choice.
    // The locale affects numerics printing and scanning, date and time.
    // Note that with other locales (e.g. "de" or "fr"), the floating-point numbers may have
    // a comma (",") as the decimal separator instead of a point (".").
    // There is also an OpenCOlorIO issue with non-C numeric locales:
    // https://github.com/imageworks/OpenColorIO/issues/297
    //
    // this must be done after initializing the QCoreApplication, see
    // https://qt-project.org/doc/qt-5/qcoreapplication.html#locale-settings

    // Set the C and C++ locales
    // see http://en.cppreference.com/w/cpp/locale/locale/global
    // Maybe this can also workaround the OSX crash in loadlocale():
    // https://discussions.apple.com/thread/3479591
    // https://github.com/cth103/dcpomatic/blob/master/src/lib/safe_stringstream.h
    // stringstreams don't seem to be thread-safe on OSX because the change the locale.

    // We also set explicitely the LC_NUMERIC locale to "C" to avoid juggling
    // between locales when using stringstreams.
    // See function __convert_from_v(...) in
    // /usr/include/c++/4.2.1/x86_64-apple-darwin10/bits/c++locale.h
    // https://www.opensource.apple.com/source/libstdcxx/libstdcxx-104.1/include/c++/4.2.1/bits/c++locale.h
    // See also https://stackoverflow.com/questions/22753707/is-ostream-operator-in-libstdc-thread-hostile

    // set the C++ locale first
    try {
        std::locale::global( std::locale(std::locale("en_US.UTF-8"), "C", std::locale::numeric) );
    } catch (std::runtime_error) {
        try {
            std::locale::global( std::locale(std::locale("C.UTF-8"), "C", std::locale::numeric) );
        } catch (std::runtime_error) {
            try {
                std::locale::global( std::locale(std::locale("UTF-8"), "C", std::locale::numeric) );
            } catch (std::runtime_error) {
                try {
                    std::locale::global( std::locale("C") );
                } catch (std::runtime_error) {
                    qDebug() << "Could not set C++ locale!";
                }
            }
        }
    }

    // set the C locale second, because it will not overwrite the changes you made to the C++ locale
    // see https://stackoverflow.com/questions/12373341/does-stdlocaleglobal-make-affect-to-printf-function
    char *category = std::setlocale(LC_ALL, "en_US.UTF-8");
    if (category == NULL) {
        category = std::setlocale(LC_ALL, "C.UTF-8");
    }
    if (category == NULL) {
        category = std::setlocale(LC_ALL, "UTF-8");
    }
    if (category == NULL) {
        category = std::setlocale(LC_ALL, "C");
    }
    if (category == NULL) {
        qDebug() << "Could not set C locale!";
    }
    std::setlocale(LC_NUMERIC, "C"); // set the locale for LC_NUMERIC only
    QLocale::setDefault( QLocale(QLocale::English, QLocale::UnitedStates) );
}

bool
AppManager::loadInternal(const CLArgs& cl)
{
    assert(!_imp->_loaded);

    _imp->_binaryPath = QCoreApplication::applicationDirPath();
    assert(StrUtils::is_utf8(_imp->_binaryPath.toStdString().c_str()));

    registerEngineMetaTypes();
    registerGuiMetaTypes();

    qApp->setOrganizationName( QString::fromUtf8(NATRON_ORGANIZATION_NAME) );
    qApp->setOrganizationDomain( QString::fromUtf8(NATRON_ORGANIZATION_DOMAIN) );
    qApp->setApplicationName( QString::fromUtf8(NATRON_APPLICATION_NAME) );

    //Set it once setApplicationName is set since it relies on it
    _imp->diskCachesLocation = StandardPaths::writableLocation(StandardPaths::eStandardLocationCache);

    // Set the locale AGAIN, because Qt resets it in the QCoreApplication constructor
    // see http://doc.qt.io/qt-4.8/qcoreapplication.html#locale-settings
    setApplicationLocale();
    
    Log::instance(); //< enable logging
    bool mustSetSignalsHandlers = true;
#ifdef NATRON_USE_BREAKPAD
    //Enabled breakpad only if the process was spawned from the crash reporter
    const QString& breakpadProcessExec = cl.getBreakpadProcessExecutableFilePath();
    if ( !breakpadProcessExec.isEmpty() && QFile::exists(breakpadProcessExec) ) {
        _imp->breakpadProcessExecutableFilePath = breakpadProcessExec;
        _imp->breakpadProcessPID = (Q_PID)cl.getBreakpadProcessPID();
        const QString& breakpadPipePath = cl.getBreakpadPipeFilePath();
        const QString& breakpadComPipePath = cl.getBreakpadComPipeFilePath();
        int breakpad_client_fd = cl.getBreakpadClientFD();
        _imp->initBreakpad(breakpadPipePath, breakpadComPipePath, breakpad_client_fd);
        mustSetSignalsHandlers = false;
    }
#endif


# ifdef __NATRON_UNIX__
    if (mustSetSignalsHandlers) {
        setShutDownSignal(SIGINT);   // shut down on ctrl-c
        setShutDownSignal(SIGTERM);   // shut down on killall
#     if defined(__NATRON_LINUX__) && !defined(__FreeBSD__)
        //Catch SIGSEGV only when google-breakpad is not active
        setSigSegvSignal();
#     endif
    }
# else
    Q_UNUSED(mustSetSignalsHandlers);
# endif


    _imp->_settings = Settings::create();
    _imp->_settings->initializeKnobsPublic();

    bool hasGLForRendering = hasOpenGLForRequirements(eOpenGLRequirementsTypeRendering, 0);
    if (_imp->hasInitializedOpenGLFunctions && hasGLForRendering) {
        OSGLContext::getGPUInfos(_imp->openGLRenderers);
        for (std::list<OpenGLRendererInfo>::iterator it = _imp->openGLRenderers.begin(); it != _imp->openGLRenderers.end(); ++it) {
            qDebug() << "Found OpenGL Renderer:" << it->rendererName.c_str() << ", Vendor:" << it->vendorName.c_str()
                     << ", OpenGL Version:" << it->glVersionString.c_str() << ", Max. Texture Size" << it->maxTextureSize <<
                ",Max GPU Memory:" << printAsRAM(it->maxMemBytes);;
        }
    }
    _imp->_settings->populateOpenGLRenderers(_imp->openGLRenderers);



    if (!cl.isLoadedUsingDefaultSettings()) {
        ///Call restore after initializing knobs
        _imp->_settings->restoreAllSettings();
    }

    ///basically show a splashScreen load fonts etc...
    return initGui(cl);
} // loadInternal

const std::list<OpenGLRendererInfo>&
AppManager::getOpenGLRenderers() const
{
    return _imp->openGLRenderers;
}

bool
AppManager::isSpawnedFromCrashReporter() const
{
#ifdef NATRON_USE_BREAKPAD

    return _imp->breakpadHandler.get() != 0;
#else

    return false;
#endif
}

void
AppManager::setPluginsUseInputImageCopyToRender(bool b)
{
    _imp->pluginsUseInputImageCopyToRender = b;
}

bool
AppManager::isCopyInputImageForPluginRenderEnabled() const
{
    return _imp->pluginsUseInputImageCopyToRender;
}

bool
AppManager::isOpenGLLoaded() const
{
    QMutexLocker k(&_imp->openGLFunctionsMutex);

    return _imp->hasInitializedOpenGLFunctions;
}

bool
AppManager::isTextureFloatSupported() const
{
    return _imp->glHasTextureFloat;
}

bool
AppManager::hasOpenGLForRequirements(OpenGLRequirementsTypeEnum type, QString* missingOpenGLError ) const
{
    std::map<OpenGLRequirementsTypeEnum,AppManagerPrivate::OpenGLRequirementsData>::const_iterator found =  _imp->glRequirements.find(type);
    assert(found != _imp->glRequirements.end());
    if (found == _imp->glRequirements.end()) {
        return false;
    }
    if (missingOpenGLError && !found->second.hasRequirements) {
        *missingOpenGLError = found->second.error;
    }
    return found->second.hasRequirements;
}

bool
AppManager::initializeOpenGLFunctionsOnce(bool createOpenGLContext)
{
    QMutexLocker k(&_imp->openGLFunctionsMutex);

    if (!_imp->hasInitializedOpenGLFunctions) {
        OSGLContextPtr glContext;
        bool checkRenderingReq = true;
        boost::shared_ptr<OSGLContextAttacher> attacher;
        if (createOpenGLContext) {
            try {
                _imp->initGLAPISpecific();

                glContext = _imp->renderingContextPool->getOrCreateOpenGLContext(false, false /*checkIfGLLoaded*/);
                if (glContext) {
                    attacher = OSGLContextAttacher::create(glContext);
                    attacher->attach();
                    // Make the context current and check its version
                } else {
                    AppManagerPrivate::OpenGLRequirementsData& vdata = _imp->glRequirements[eOpenGLRequirementsTypeViewer];
                    AppManagerPrivate::OpenGLRequirementsData& rdata = _imp->glRequirements[eOpenGLRequirementsTypeRendering];
                    rdata.error = tr("Error creating OpenGL context.");
                    vdata.error = rdata.error;
                    rdata.hasRequirements = false;
                    vdata.hasRequirements = false;
                    AppManagerPrivate::addOpenGLRequirementsString(rdata.error, eOpenGLRequirementsTypeRendering);
                    AppManagerPrivate::addOpenGLRequirementsString(vdata.error, eOpenGLRequirementsTypeViewer);
                }


            } catch (const std::exception& e) {
                std::cerr << "Error while loading OpenGL: " << e.what() << std::endl;
                std::cerr << "OpenGL rendering is disabled. " << std::endl;
                AppManagerPrivate::OpenGLRequirementsData& vdata = _imp->glRequirements[eOpenGLRequirementsTypeViewer];
                AppManagerPrivate::OpenGLRequirementsData& rdata = _imp->glRequirements[eOpenGLRequirementsTypeRendering];
                rdata.hasRequirements = false;
                vdata.hasRequirements = false;
                vdata.error = tr("Error while creating OpenGL context: %1").arg(QString::fromUtf8(e.what()));
                rdata.error = tr("Error while creating OpenGL context: %1").arg(QString::fromUtf8(e.what()));
                AppManagerPrivate::addOpenGLRequirementsString(rdata.error, eOpenGLRequirementsTypeRendering);
                AppManagerPrivate::addOpenGLRequirementsString(vdata.error, eOpenGLRequirementsTypeViewer);
                checkRenderingReq = false;
            }
            if (!glContext) {
                return false;
            }
        }

        // The following requires a valid OpenGL context to be created
        _imp->initGl(checkRenderingReq);

        // Load our OpenGL functions both in OSMesa and GL (from glad)
        GL_GPU::load();
        GL_CPU::load();


        if (createOpenGLContext) {
            if (hasOpenGLForRequirements(eOpenGLRequirementsTypeRendering)) {
                try {
                    OSGLContext::checkOpenGLVersion(true);
                } catch (const std::exception& e) {
                    AppManagerPrivate::OpenGLRequirementsData& data = _imp->glRequirements[eOpenGLRequirementsTypeRendering];
                    data.hasRequirements = false;
                    if ( !data.error.isEmpty() ) {
                        data.error = QString::fromUtf8( e.what() );
                    }
                }
            }
            

            // Deattach the context
            if (attacher) {
                attacher.reset();
            }

            // Clear created contexts because this context was created with the "default" OpenGL renderer and it might be different from the one
            // selected by the user in the settings (which are not created yet).
            _imp->renderingContextPool->clear();
        } else {
            updateAboutWindowLibrariesVersion();
        }

        return true;
    }

    return false;
}

int
AppManager::getOpenGLVersionMajor() const
{
    return _imp->glVersionMajor;
}

int
AppManager::getOpenGLVersionMinor() const
{
    return _imp->glVersionMinor;
}

#ifdef __NATRON_WIN32__
const OSGLContext_wgl_data*
AppManager::getWGLData() const
{
    return _imp->wglInfo.get();
}

#endif
#ifdef __NATRON_LINUX__
const OSGLContext_glx_data*
AppManager::getGLXData() const
{
    return _imp->glxInfo.get();
}

#endif


bool
AppManager::initGui(const CLArgs& cl)
{
    ///In background mode, directly call the rest of the loading code
    return loadInternalAfterInitGui(cl);
}

bool
AppManager::loadInternalAfterInitGui(const CLArgs& cl)
{
    try {
        size_t maxCacheRAM = _imp->_settings->getRamMaximumPercent() * getSystemTotalRAM();
        U64 viewerCacheSize = _imp->_settings->getMaximumViewerDiskCacheSize();
        U64 maxDiskCacheNode = _imp->_settings->getMaximumDiskCacheNodeSize();

        _imp->_nodeCache.reset( new ImageCache("NodeCache", NATRON_CACHE_VERSION, maxCacheRAM, 1.) );
        _imp->_diskCache.reset( new ImageCache("DiskCache", NATRON_CACHE_VERSION, maxDiskCacheNode, 0.) );
        _imp->_viewerCache.reset( new FrameEntryCache("ViewerCache", NATRON_CACHE_VERSION, viewerCacheSize, 0.) );
        _imp->setViewerCacheTileSize();
    } catch (std::logic_error) {
        // ignore
    }

    int oldCacheVersion = 0;
    {
        QSettings settings( QString::fromUtf8(NATRON_ORGANIZATION_NAME), QString::fromUtf8(NATRON_APPLICATION_NAME) );

        if ( settings.contains( QString::fromUtf8(kNatronCacheVersionSettingsKey) ) ) {
            oldCacheVersion = settings.value( QString::fromUtf8(kNatronCacheVersionSettingsKey) ).toInt();
        }
        settings.setValue(QString::fromUtf8(kNatronCacheVersionSettingsKey), NATRON_CACHE_VERSION);
    }

    setLoadingStatus( tr("Restoring the image cache...") );

    if (oldCacheVersion != NATRON_CACHE_VERSION) {
        wipeAndCreateDiskCacheStructure();
    } else {
        _imp->restoreCaches();
    }

    setLoadingStatus( tr("Loading plug-in cache...") );


    ///Set host properties after restoring settings since it depends on the host name.
    try {
        _imp->ofxHost->setProperties();
    } catch (std::logic_error) {
        // ignore
    }

    /*loading all plugins*/
    try {
        loadAllPlugins();
        _imp->loadBuiltinFormats();
    } catch (std::logic_error) {
        // ignore
    }

    if ( isBackground() && !cl.getIPCPipeName().isEmpty() ) {
        _imp->initProcessInputChannel( cl.getIPCPipeName() );
    }


    if ( cl.isInterpreterMode() ) {
        _imp->_appType = eAppTypeInterpreter;
    } else if ( isBackground() ) {
        if ( !cl.getScriptFilename().isEmpty() ) {
            if ( !cl.getIPCPipeName().isEmpty() ) {
                _imp->_appType = eAppTypeBackgroundAutoRunLaunchedFromGui;
            } else {
                _imp->_appType = eAppTypeBackgroundAutoRun;
            }
        } else {
            _imp->_appType = eAppTypeBackground;
        }
    } else {
        _imp->_appType = eAppTypeGui;
    }

    //Now that the locale is set, re-parse the command line arguments because the filenames might have non UTF-8 encodings
    CLArgs args;
    if ( !cl.getScriptFilename().isEmpty() ) {
        const QStringList& appArgs = qApp->arguments();
        args = CLArgs( appArgs, cl.isBackgroundMode() );
    } else {
        args = cl;
    }

    AppInstancePtr mainInstance = newAppInstance(args, false);

    hideSplashScreen();

    if (!mainInstance) {
        qApp->quit();

        return false;
    } else {
        onLoadCompleted();

        ///In background project auto-run the rendering is finished at this point, just exit the instance
        if ( ( (_imp->_appType == eAppTypeBackgroundAutoRun) ||
               ( _imp->_appType == eAppTypeBackgroundAutoRunLaunchedFromGui) ||
               ( _imp->_appType == eAppTypeInterpreter) ) && mainInstance ) {
            bool wasKilled = true;
            const AppInstanceVec& instances = appPTR->getAppInstances();
            for (AppInstanceVec::const_iterator it = instances.begin(); it != instances.end(); ++it) {
                if ( (*it == mainInstance) ) {
                    wasKilled = false;
                }
            }
            if (!wasKilled) {
                try {
                    mainInstance->getProject()->reset(true/*aboutToQuit*/, true /*blocking*/);
                } catch (std::logic_error) {
                    // ignore
                }

                try {
                    mainInstance->quitNow();
                } catch (std::logic_error) {
                    // ignore
                }
            }
        }

        return true;
    }
} // AppManager::loadInternalAfterInitGui

void
AppManager::onViewerTileCacheSizeChanged()
{
    if (_imp->_viewerCache) {
        _imp->_viewerCache->clear();
        _imp->setViewerCacheTileSize();
    }
    for (AppInstanceVec::const_iterator it = _imp->_appInstances.begin(); it != _imp->_appInstances.end(); ++it) {
        (*it)->renderAllViewers(true);
    }
}

void
AppManagerPrivate::setViewerCacheTileSize()
{
    if (!_viewerCache) {
        return;
    }
    std::size_t tileSize =  (std::size_t)std::pow( 2., (double)_settings->getViewerTilesPowerOf2() );

    // Viewer tiles are always RGBA
    tileSize = tileSize * tileSize * 4;


    ImageBitDepthEnum viewerDepth = _settings->getViewersBitDepth();
    switch (viewerDepth) {
        case eImageBitDepthFloat:
        case eImageBitDepthHalf:
            tileSize *= sizeof(float);
            break;
        default:
            break;
    }
    _viewerCache->setTiled(true, tileSize);
}

AppInstancePtr
AppManager::newAppInstanceInternal(const CLArgs& cl,
                                   bool alwaysBackground,
                                   bool makeEmptyInstance)
{
    AppInstancePtr instance;

    if (!alwaysBackground) {
        instance = makeNewInstance(_imp->_availableID);
    } else {
        instance = AppInstance::create(_imp->_availableID);
    }

    {
        QMutexLocker k(&_imp->_appInstancesMutex);
        _imp->_appInstances.push_back(instance);
    }

    setAsTopLevelInstance( instance->getAppID() );

    ++_imp->_availableID;

    try {
        instance->load(cl, makeEmptyInstance);
    } catch (const std::exception & e) {
        Dialogs::errorDialog( NATRON_APPLICATION_NAME, e.what(), false );
        removeInstance(_imp->_availableID);
        instance.reset();
        --_imp->_availableID;

        return instance;
    } catch (...) {
        Dialogs::errorDialog( NATRON_APPLICATION_NAME, tr("Cannot load project").toStdString(), false );
        removeInstance(_imp->_availableID);
        instance.reset();
        --_imp->_availableID;

        return instance;
    }

    ///flag that we finished loading the Appmanager even if it was already true
    _imp->_loaded = true;

    return instance;
}

AppInstancePtr
AppManager::newBackgroundInstance(const CLArgs& cl,
                                  bool makeEmptyInstance)
{
    return newAppInstanceInternal(cl, true, makeEmptyInstance);
}

AppInstancePtr
AppManager::newAppInstance(const CLArgs& cl,
                           bool makeEmptyInstance)
{
    return newAppInstanceInternal(cl, false, makeEmptyInstance);
}

AppInstancePtr
AppManager::getAppInstance(int appID) const
{
    QMutexLocker k(&_imp->_appInstancesMutex);

    for (AppInstanceVec::const_iterator it = _imp->_appInstances.begin(); it != _imp->_appInstances.end(); ++it) {
        if ( (*it)->getAppID() == appID ) {
            return *it;
        }
    }

    return AppInstancePtr();
}

int
AppManager::getNumInstances() const
{
    QMutexLocker k(&_imp->_appInstancesMutex);

    return (int)_imp->_appInstances.size();
}

const AppInstanceVec &
AppManager::getAppInstances() const
{
    assert( QThread::currentThread() == qApp->thread() );

    return _imp->_appInstances;
}

void
AppManager::removeInstance(int appID)
{
    int newApp = -1;
    {
        QMutexLocker k(&_imp->_appInstancesMutex);
        for (AppInstanceVec::iterator it = _imp->_appInstances.begin(); it != _imp->_appInstances.end(); ++it) {
            if ( (*it)->getAppID() == appID ) {
                _imp->_appInstances.erase(it);
                break;
            }
        }

        if ( !_imp->_appInstances.empty() ) {
            newApp = _imp->_appInstances.front()->getAppID();
        }
    }

    if (newApp != -1) {
        setAsTopLevelInstance(newApp);
    }
}

AppManager::AppTypeEnum
AppManager::getAppType() const
{
    return _imp->_appType;
}

void
AppManager::clearPlaybackCache()
{
    if (!_imp->_viewerCache) {
        return;
    }
    _imp->_viewerCache->clearInMemoryPortion();
    clearLastRenderedTextures();
}

void
AppManager::clearViewerCache()
{
    if (!_imp->_viewerCache) {
        return;
    }

    _imp->_viewerCache->clear();
}

void
AppManager::clearDiskCache()
{
    if (!_imp->_viewerCache) {
        return;
    }

    clearLastRenderedTextures();
    _imp->_viewerCache->clear();
    _imp->_diskCache->clear();
}

void
AppManager::clearNodeCache()
{
    AppInstanceVec copy;
    {
        QMutexLocker k(&_imp->_appInstancesMutex);
        copy = _imp->_appInstances;
    }

    for (AppInstanceVec::iterator it = copy.begin(); it != copy.end(); ++it) {
        (*it)->clearAllLastRenderedImages();
    }
    _imp->_nodeCache->clear();
}

void
AppManager::clearPluginsLoadedCache()
{
    _imp->ofxHost->clearPluginsLoadedCache();
}

void
AppManager::clearAllCaches()
{
    AppInstanceVec copy;
    {
        QMutexLocker k(&_imp->_appInstancesMutex);
        copy = _imp->_appInstances;
    }

    for (AppInstanceVec::iterator it = copy.begin(); it != copy.end(); ++it) {
        (*it)->abortAllViewers();
    }

    clearDiskCache();
    clearNodeCache();


    ///for each app instance clear all its nodes cache
    for (AppInstanceVec::iterator it = copy.begin(); it != copy.end(); ++it) {
        (*it)->clearOpenFXPluginsCaches();
    }

    for (AppInstanceVec::iterator it = copy.begin(); it != copy.end(); ++it) {
        (*it)->renderAllViewers(true);
    }

    Project::clearAutoSavesDir();
}

void
AppManager::wipeAndCreateDiskCacheStructure()
{
    //Should be called on the main-thread because potentially can interact with rendering
    assert( QThread::currentThread() == qApp->thread() );

    abortAnyProcessing();

    clearAllCaches();

    assert(_imp->_diskCache);
    _imp->cleanUpCacheDiskStructure( _imp->_diskCache->getCachePath(), false );
    assert(_imp->_viewerCache);
    _imp->cleanUpCacheDiskStructure( _imp->_viewerCache->getCachePath() , true);
}

AppInstancePtr
AppManager::getTopLevelInstance () const
{
    QMutexLocker k(&_imp->_appInstancesMutex);

    for (AppInstanceVec::const_iterator it = _imp->_appInstances.begin(); it != _imp->_appInstances.end(); ++it) {
        if ( (*it)->getAppID() == _imp->_topLevelInstanceID ) {
            return *it;
        }
    }

    return AppInstancePtr();
}

bool
AppManager::isLoaded() const
{
    return _imp->_loaded;
}

void
AppManager::abortAnyProcessing()
{
    AppInstanceVec copy;
    {
        QMutexLocker k(&_imp->_appInstancesMutex);
        copy = _imp->_appInstances;
    }

    for (AppInstanceVec::iterator it = copy.begin(); it != copy.end(); ++it) {
        (*it)->getProject()->quitAnyProcessingForAllNodes_non_blocking();
    }
}

bool
AppManager::writeToOutputPipe(const QString & longMessage,
                              const QString & shortMessage,
                              bool printIfNoChannel)
{
    if (!_imp->_backgroundIPC) {
        if (printIfNoChannel) {
            QMutexLocker k(&_imp->errorLogMutex);
            ///Don't use qdebug here which is disabled if QT_NO_DEBUG_OUTPUT is defined.
            std::cout << longMessage.toStdString() << std::endl;
        }

        return false;
    }
    _imp->_backgroundIPC->writeToOutputChannel(shortMessage);

    return true;
}

void
AppManager::setApplicationsCachesMaximumMemoryPercent(double p)
{
    size_t maxCacheRAM = p * getSystemTotalRAM_conditionnally();

    _imp->_nodeCache->setMaximumCacheSize(maxCacheRAM);
    _imp->_nodeCache->setMaximumInMemorySize(1);
}

void
AppManager::setApplicationsCachesMaximumViewerDiskSpace(unsigned long long size)
{
    _imp->_viewerCache->setMaximumCacheSize(size);
}

void
AppManager::setApplicationsCachesMaximumDiskSpace(unsigned long long size)
{
    _imp->_diskCache->setMaximumCacheSize(size);
}

void
AppManager::loadAllPlugins()
{
    assert( _imp->_plugins.empty() );
    assert( _imp->_formats.empty() );

    // Load plug-ins bundled into Natron
    loadBuiltinNodePlugins(&_imp->readerPlugins, &_imp->writerPlugins);

    // Load OpenFX plug-ins
    _imp->ofxHost->loadOFXPlugins( &_imp->readerPlugins, &_imp->writerPlugins);

    _imp->declareSettingsToPython();

    // Load PyPlugs and init.py & initGui.py scripts
    // Should be done after settings are declared
    loadPythonGroups();

    // Load presets after all plug-ins are loaded
    loadNodesPresets();

    _imp->_settings->restorePluginSettings();


    onAllPluginsLoaded();
}

void
AppManager::onAllPluginsLoaded()
{
    //We try to make nicer plug-in labels, only do this if the user use Natron with some sort of interaction (either command line
    //or GUI, otherwise don't bother doing this)

    AppManager::AppTypeEnum appType = appPTR->getAppType();

    if ( (appType != AppManager::eAppTypeBackground) &&
         ( appType != AppManager::eAppTypeGui) &&
         ( appType != AppManager::eAppTypeInterpreter) ) {
        return;
    }

    //Make sure there is no duplicates with the same label
    const PluginsMap& plugins = getPluginsList();
    for (PluginsMap::const_iterator it = plugins.begin(); it != plugins.end(); ++it) {
        assert( !it->second.empty() );
        if (it->second.empty()) {
            continue;
        }
    
        PluginMajorsOrdered::iterator first = it->second.begin();

        // If at least one version of the plug-in can be created, consider it creatable
        bool isUserCreatable = false;
        for (PluginMajorsOrdered::iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
            if ( (*it2)->getIsUserCreatable() ) {
                isUserCreatable = true;
            } else {
                (*it2)->setLabelWithoutSuffix((*it2)->getPluginLabel());
            }
        }
        if (!isUserCreatable) {
            continue;
        }

        std::string labelWithoutSuffix = Plugin::makeLabelWithoutSuffix( (*first)->getPluginLabel() );

        // Find a duplicate
        for (PluginsMap::const_iterator it2 = plugins.begin(); it2 != plugins.end(); ++it2) {
            if (it->first == it2->first) {
                continue;
            }


            PluginMajorsOrdered::iterator other = it2->second.begin();
            bool isOtherUserCreatable = false;
            for (PluginMajorsOrdered::iterator it3 = it2->second.begin(); it3 != it2->second.end(); ++it3) {
                if ( (*it3)->getIsUserCreatable() ) {
                    isOtherUserCreatable = true;
                    break;
                }
            }

            if (!isOtherUserCreatable) {
                continue;
            }

            // If we find another plug-in (with a different ID) but with the same label without suffix and same grouping
            // then keep the original label
            std::string otherLabelWithoutSuffix = Plugin::makeLabelWithoutSuffix( (*other)->getPluginLabel() );
            if (otherLabelWithoutSuffix == labelWithoutSuffix) {

                std::vector<std::string> otherGrouping = (*other)->getPropertyN<std::string>(kNatronPluginPropGrouping);
                std::vector<std::string> thisGrouping = (*first)->getPropertyN<std::string>(kNatronPluginPropGrouping);
                bool allEqual = false;
                if (otherGrouping.size() == thisGrouping.size()) {
                    allEqual = true;
                    for (std::size_t i = 0; i < thisGrouping.size(); ++i) {
                        if (otherGrouping[i] != thisGrouping[i]) {
                            allEqual = false;
                            break;
                        }
                    }
                }
                if (allEqual) {
                    labelWithoutSuffix = (*first)->getPluginLabel();
                }
                break;
            }
        }


        for (PluginMajorsOrdered::reverse_iterator it2 = it->second.rbegin(); it2 != it->second.rend(); ++it2) {
            if (it2 == it->second.rbegin()) {
                // This is the highest major version loaded for that plug-in
                (*it2)->setIsHighestMajorVersion(true);
            }
            if ( (*it2)->getIsUserCreatable() ) {
                (*it2)->setLabelWithoutSuffix(labelWithoutSuffix);
                onPluginLoaded(*it2);
            } else {

            }
        }
    }
} // AppManager::onAllPluginsLoaded


void
AppManager::loadBuiltinNodePlugins(IOPluginsMap* /*readersMap*/,
                                   IOPluginsMap* /*writersMap*/)
{
    registerPlugin(Backdrop::createPlugin());
    registerPlugin(GroupOutput::createPlugin());
    registerPlugin(GroupInput::createPlugin());
    registerPlugin(NodeGroup::createPlugin());
    registerPlugin(Dot::createPlugin());
    registerPlugin(DiskCacheNode::createPlugin());
    registerPlugin(RotoPaint::createPlugin());
    registerPlugin(RotoNode::createPlugin());
    registerPlugin(LayeredCompNode::createPlugin());
    registerPlugin(RotoShapeRenderNode::createPlugin());
    registerPlugin(PrecompNode::createPlugin());
    registerPlugin(TrackerNode::createPlugin());
    registerPlugin(JoinViewsNode::createPlugin());
    registerPlugin(OneViewNode::createPlugin());
    registerPlugin(ReadNode::createPlugin());
    registerPlugin(StubNode::createPlugin());
    registerPlugin(WriteNode::createPlugin());
    registerPlugin(ViewerNode::createPlugin());
    registerPlugin(ViewerInstance::createPlugin());


}

bool
AppManager::findAndRunScriptFile(const QString& path,
                     const QStringList& files,
                     const QString& script)
{
#ifdef NATRON_RUN_WITHOUT_PYTHON

    return false;
#endif
    for (QStringList::const_iterator it = files.begin(); it != files.end(); ++it) {
        if (*it == script) {
            QString absolutePath = path + *it;
            QFile file(absolutePath);
            if ( file.open(QIODevice::ReadOnly) ) {
                QTextStream ts(&file);
                QString content = ts.readAll();
                PyRun_SimpleString( content.toStdString().c_str() );


                PyObject* mainModule = NATRON_PYTHON_NAMESPACE::getMainModule();
                std::string error, output;

                ///Gui session, do stdout, stderr redirection
                PyObject *errCatcher = 0;
                PyObject *outCatcher = 0;

                if ( PyObject_HasAttrString(mainModule, "catchErr") ) {
                    errCatcher = PyObject_GetAttrString(mainModule, "catchErr"); //get our catchOutErr created above, new ref
                }

                if ( PyObject_HasAttrString(mainModule, "catchOut") ) {
                    outCatcher = PyObject_GetAttrString(mainModule, "catchOut"); //get our catchOutErr created above, new ref
                }

                PyErr_Print(); //make python print any errors

                PyObject *errorObj = 0;
                if (errCatcher) {
                    errorObj = PyObject_GetAttrString(errCatcher, "value"); //get the  stderr from our catchErr object, new ref
                    assert(errorObj);

                    error = NATRON_PYTHON_NAMESPACE::PyStringToStdString(errorObj);
                    PyObject* unicode = PyUnicode_FromString("");
                    PyObject_SetAttrString(errCatcher, "value", unicode);
                    Py_DECREF(errorObj);
                    Py_DECREF(errCatcher);
                }
                PyObject *outObj = 0;
                if (outCatcher) {
                    outObj = PyObject_GetAttrString(outCatcher, "value"); //get the stdout from our catchOut object, new ref
                    assert(outObj);
                    output = NATRON_PYTHON_NAMESPACE::PyStringToStdString(outObj);
                    PyObject* unicode = PyUnicode_FromString("");
                    PyObject_SetAttrString(outCatcher, "value", unicode);
                    Py_DECREF(outObj);
                    Py_DECREF(outCatcher);
                }


                if ( !error.empty() ) {
                    QString message(tr("Failed to load %1: %2").arg(absolutePath).arg(QString::fromUtf8( error.c_str() )) );
                    appPTR->writeToErrorLog_mt_safe(tr("Python Script"), QDateTime::currentDateTime(), message, false);
                    std::cerr << message.toStdString() << std::endl;

                    return false;
                }
                if ( !output.empty() ) {
                    QString message;
                    message.append(absolutePath);
                    message.append( QString::fromUtf8(": ") );
                    message.append( QString::fromUtf8( output.c_str() ) );
                    if ( appPTR->getTopLevelInstance() ) {
                        appPTR->getTopLevelInstance()->appendToScriptEditor( message.toStdString() );
                    }
                    std::cout << message.toStdString() << std::endl;
                }

                return true;
            }
            break;
        }
    }

    return false;
} // findAndRunScriptFile

QStringList
AppManager::getAllNonOFXPluginsPaths() const
{
    QStringList templatesSearchPath;

    //add ~/.Natron
    QString dataLocation = QDir::homePath();
    QString mainPath = dataLocation + QString::fromUtf8("/.") + QString::fromUtf8(NATRON_APPLICATION_NAME);
    QDir mainPathDir(mainPath);

    if ( !mainPathDir.exists() ) {
        QDir dataDir(dataLocation);
        if ( dataDir.exists() ) {
            dataDir.mkdir( QChar::fromLatin1('.') + QString( QString::fromUtf8(NATRON_APPLICATION_NAME) ) );
        }
    }

    QString envvar( QString::fromUtf8( qgetenv(NATRON_PATH_ENV_VAR) ) );
#ifdef __NATRON_WIN32__
    QStringList splitDirs = envvar.split( QChar::fromLatin1(';') );
#else
    QStringList splitDirs = envvar.split( QChar::fromLatin1(':') );
#endif
    std::list<std::string> userSearchPaths;
    _imp->_settings->getPythonGroupsSearchPaths(&userSearchPaths);


    //This is the bundled location for PyPlugs
    QDir cwd( QCoreApplication::applicationDirPath() );
    cwd.cdUp();
    QString natronBundledPluginsPath = QString( cwd.absolutePath() +  QString::fromUtf8("/Plugins/PyPlugs") );
    bool preferBundleOverSystemWide = _imp->_settings->preferBundledPlugins();
    bool useBundledPlugins = _imp->_settings->loadBundledPlugins();
    if (preferBundleOverSystemWide && useBundledPlugins) {
        ///look-in the bundled plug-ins
        templatesSearchPath.push_back(natronBundledPluginsPath);
    }

    ///look-in the main system wide plugin path
    templatesSearchPath.push_back(mainPath);

    ///look-in the global system wide plugin path
    templatesSearchPath.push_back( getPyPlugsGlobalPath() );

    ///look-in the locations indicated by NATRON_PLUGIN_PATH
    Q_FOREACH(const QString &splitDir, splitDirs) {
        if ( !splitDir.isEmpty() ) {
            templatesSearchPath.push_back(splitDir);
        }
    }

    ///look-in extra search path set in the preferences
    for (std::list<std::string>::iterator it = userSearchPaths.begin(); it != userSearchPaths.end(); ++it) {
        if ( !it->empty() ) {
            templatesSearchPath.push_back( QString::fromUtf8( it->c_str() ) );
        }
    }

    if (!preferBundleOverSystemWide && useBundledPlugins) {
        ///look-in the bundled plug-ins
        templatesSearchPath.push_back(natronBundledPluginsPath);
    }

    return templatesSearchPath;
} // AppManager::getAllNonOFXPluginsPaths

QString
AppManager::getPyPlugsGlobalPath() const
{
    QString path;

#ifdef __NATRON_UNIX__
#ifdef __NATRON_OSX__
    path = QString::fromUtf8("/Library/Application Support/%1/Plugins").arg( QString::fromUtf8(NATRON_APPLICATION_NAME) );
#else
    path = QString::fromUtf8("/usr/share/%1/Plugins").arg( QString::fromUtf8(NATRON_APPLICATION_NAME) );
#endif
#elif defined(__NATRON_WIN32__)
    wchar_t buffer[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES_COMMON, NULL, SHGFP_TYPE_CURRENT, buffer);
    std::wstring str;
    str.append(L"\\");
    str.append( QString::fromUtf8("%1\\Plugins").arg( QString::fromUtf8(NATRON_APPLICATION_NAME) ).toStdWString() );
    wcscat_s(buffer, MAX_PATH, str.c_str());
    path = QString::fromStdWString( std::wstring(buffer) );
#endif

    return path;
}

typedef void (*NatronPathFunctor)(const QDir&);
static void
operateOnPathRecursive(NatronPathFunctor functor,
                       const QDir& directory)
{
    if ( !directory.exists() ) {
        return;
    }

    functor(directory);

    QStringList subDirs = directory.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
    Q_FOREACH(const QString &subDir, subDirs) {
        QDir d(directory.absolutePath() + QChar::fromLatin1('/') + subDir);

        operateOnPathRecursive(functor, d);
    }
}

static void
addToPythonPathFunctor(const QDir& directory)
{
    std::string addToPythonPath("sys.path.append(str('");

    addToPythonPath += directory.absolutePath().toStdString();
    addToPythonPath += "').decode('utf-8'))\n";

    std::string err;
    bool ok  = NATRON_PYTHON_NAMESPACE::interpretPythonScript(addToPythonPath, &err, 0);
    if (!ok) {
        std::string message = QCoreApplication::translate("AppManager", "Could not add %1 to python path:").arg( directory.absolutePath() ).toStdString() + ' ' + err;
        std::cerr << message << std::endl;
        AppInstancePtr topLevel = appPTR->getTopLevelInstance();
        if (topLevel) {
            topLevel->appendToScriptEditor( message.c_str() );
        }
    }
}

void
AppManager::findAllScriptsRecursive(const QDir& directory,
                        QStringList& allPlugins,
                        QStringList *foundInit,
                        QStringList *foundInitGui)
{
    if ( !directory.exists() ) {
        return;
    }

    QStringList filters;
    filters << QString::fromUtf8("*.py");
    QStringList files = directory.entryList(filters, QDir::Files | QDir::NoDotAndDotDot);
    bool ok = findAndRunScriptFile( directory.absolutePath() + QChar::fromLatin1('/'), files, QString::fromUtf8("init.py") );
    if (ok) {
        foundInit->append( directory.absolutePath() + QString::fromUtf8("/init.py") );
    }
    if ( !appPTR->isBackground() ) {
        ok = findAndRunScriptFile( directory.absolutePath() + QChar::fromLatin1('/'), files, QString::fromUtf8("initGui.py") );
        if (ok) {
            foundInitGui->append( directory.absolutePath() + QString::fromUtf8("/initGui.py") );
        }
    }

    for (QStringList::iterator it = files.begin(); it != files.end(); ++it) {
        if ( it->endsWith( QString::fromUtf8(".py") ) && ( *it != QString::fromUtf8("init.py") ) && ( *it != QString::fromUtf8("initGui.py") ) ) {
            allPlugins.push_back(directory.absolutePath() + QChar::fromLatin1('/') + *it);
        }
    }

    QStringList subDirs = directory.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
    Q_FOREACH(const QString &subDir, subDirs) {
        QDir d(directory.absolutePath() + QChar::fromLatin1('/') + subDir);

        findAllScriptsRecursive(d, allPlugins, foundInit, foundInitGui);
    }
}


void
AppManager::findAllPresetsRecursive(const QDir& directory,
                             QStringList& presetFiles)
{
    if ( !directory.exists() ) {
        return;
    }

    QStringList filters;
    filters << QString::fromUtf8("*." NATRON_PRESETS_FILE_EXT);
    QStringList files = directory.entryList(filters, QDir::Files | QDir::NoDotAndDotDot);

    for (QStringList::iterator it = files.begin(); it != files.end(); ++it) {
        if ( it->endsWith( QString::fromUtf8("." NATRON_PRESETS_FILE_EXT) )) {
            presetFiles.push_back(directory.absolutePath() + QChar::fromLatin1('/') + *it);
        }
    }
}

void
AppManager::loadNodesPresets()
{
    QStringList presetFiles;

    QStringList templatesSearchPath = getAllNonOFXPluginsPaths();
    Q_FOREACH(const QString &templatesSearchDir, templatesSearchPath) {
        QDir d(templatesSearchDir);
        findAllPresetsRecursive(d, presetFiles);
    }

    Q_FOREACH(const QString &presetFile, presetFiles) {

        FStreamsSupport::ifstream ifile;
        FStreamsSupport::open(&ifile, presetFile.toStdString());
        if (!ifile) {
            continue;
        }
        SERIALIZATION_NAMESPACE::NodeSerialization obj;
        try {
            if (!SERIALIZATION_NAMESPACE::read(NATRON_PRESETS_FILE_HEADER, ifile, &obj)) {
                continue;
            }
        } catch (...) {
            continue;
        }

        if (!obj._presetsIdentifierLabel.empty()) {
            // If the preset label is set, append as a preset of an existing plug-in
            PluginPtr foundPlugin;
            try {
                foundPlugin = getPluginBinary(QString::fromUtf8(obj._pluginID.c_str()), obj._pluginMajorVersion, obj._pluginMinorVersion, false);
            } catch (...) {
                continue;
            }
            if (!foundPlugin) {
                continue;
            }
            PluginPresetDescriptor preset;
            preset.presetFilePath = presetFile;
            preset.presetLabel = QString::fromUtf8(obj._presetsIdentifierLabel.c_str());
            preset.presetIconFile = QString::fromUtf8(obj._presetsIconFilePath.c_str());
            preset.symbol = (Key)obj._presetShortcutSymbol;
            preset.modifiers = KeyboardModifiers(obj._presetShortcutPresetModifiers);
            foundPlugin->addPresetFile(preset);
        } else {
            // Try to find a pyplug
            std::string pyPlugID, pyPlugLabel, pyPlugDescription, pyPlugIconFilePath, pyPlugGrouping, pyPlugExtCallbacks;
            bool pyPlugDescIsMarkdown = false;
            int pyPlugShortcutSymbol = 0;
            int pyPlugShortcutModifiers = 0;
            int pyPlugVersionMajor = 0,pyPlugVersionMinor = 0;
            for (SERIALIZATION_NAMESPACE::KnobSerializationList::const_iterator it = obj._knobsValues.begin(); it != obj._knobsValues.end(); ++it) {
                if ((*it)->_values.empty()) {
                    continue;
                }
                const SERIALIZATION_NAMESPACE::KnobSerialization::PerDimensionValueSerializationVec& dimVec = (*it)->_values.begin()->second;
                const SERIALIZATION_NAMESPACE::SerializationValueVariant& value0 = dimVec[0]._value;
                if ((*it)->_scriptName == kNatronNodeKnobPyPlugPluginID) {
                    pyPlugID = value0.isString;
                } else if ((*it)->_scriptName == kNatronNodeKnobPyPlugPluginLabel) {
                    pyPlugLabel = value0.isString;
                } else if ((*it)->_scriptName == kNatronNodeKnobPyPlugPluginDescription) {
                    pyPlugDescription = value0.isString;
                } else if ((*it)->_scriptName == kNatronNodeKnobPyPlugPluginDescriptionIsMarkdown) {
                    pyPlugDescIsMarkdown = value0.isBool;
                } else if ((*it)->_scriptName == kNatronNodeKnobPyPlugPluginGrouping) {
                    pyPlugGrouping = value0.isString;
                } else if ((*it)->_scriptName == kNatronNodeKnobPyPlugPluginIconFile) {
                    pyPlugIconFilePath = value0.isString;
                } else if ((*it)->_scriptName == kNatronNodeKnobPyPlugPluginCallbacksPythonScript) {
                    pyPlugExtCallbacks = value0.isString;
                } else if ((*it)->_scriptName == kNatronNodeKnobPyPlugPluginShortcut) {
                    pyPlugShortcutSymbol = value0.isInt;
                    if (dimVec.size() > 1) {
                        pyPlugShortcutModifiers = dimVec[1]._value.isInt;
                    }
                } else if ((*it)->_scriptName == kNatronNodeKnobPyPlugPluginVersion) {
                    pyPlugVersionMajor = value0.isInt;
                    if (dimVec.size() > 1) {
                        pyPlugVersionMinor = dimVec[1]._value.isInt;
                    }
                    
                }
            }

            if (!pyPlugID.empty()) {
                // If the pyPlugID is set, make a new plug-in
                // Use grouping if set, otherwise make a "PyPlug" group as a fallback
                std::vector<std::string> grouping;
                if (!pyPlugGrouping.empty()) {
                    boost::split(grouping, pyPlugGrouping, boost::is_any_of("/"));
                } else {
                    grouping.push_back("PyPlugs");
                }


                PluginPtr p = Plugin::create(0, pyPlugID, pyPlugLabel, pyPlugVersionMajor, pyPlugVersionMinor, grouping);
                if (!obj._pluginID.empty()) {
                    p->setProperty<std::string>(kNatronPluginPropPyPlugContainerID, obj._pluginID);
                }
                p->setProperty<std::string>(kNatronPluginPropPyPlugScriptAbsoluteFilePath, presetFile.toStdString());


                QString presetDirectory;
                {
                    int foundSlash = presetFile.lastIndexOf(QLatin1Char('/'));
                    if (foundSlash != -1) {
                        presetDirectory = presetFile.mid(0, foundSlash);
                    }
                }
                p->setProperty<std::string>(kNatronPluginPropResourcesPath, presetDirectory.toStdString());
                p->setProperty<bool>(kNatronPluginPropDescriptionIsMarkdown, pyPlugDescIsMarkdown);
                p->setProperty<std::string>(kNatronPluginPropDescription, pyPlugDescription);
                p->setProperty<std::string>(kNatronPluginPropIconFilePath, pyPlugIconFilePath);
                p->setProperty<int>(kNatronPluginPropShortcut, pyPlugShortcutSymbol, 0);
                p->setProperty<int>(kNatronPluginPropShortcut, pyPlugShortcutModifiers, 1);
                p->setProperty<std::string>(kNatronPluginPropPyPlugExtScriptFile, pyPlugExtCallbacks);
                p->setProperty<unsigned int>(kNatronPluginPropVersion, (unsigned int)pyPlugVersionMajor, 0);
                p->setProperty<unsigned int>(kNatronPluginPropVersion, (unsigned int)pyPlugVersionMinor, 1);
                registerPlugin(p);
                
                
            }
        }
    }
}

void
AppManager::loadPythonGroups()
{
#ifdef NATRON_RUN_WITHOUT_PYTHON

    return;
#endif
    PythonGILLocker pgl;
    QStringList templatesSearchPath = getAllNonOFXPluginsPaths();
    std::string err;
    QStringList allPlugins;

    ///For all search paths, first add the path to the python path, then run in order the init.py and initGui.py
    Q_FOREACH(const QString &templatesSearchDir, templatesSearchPath) {
        //Adding Qt resources to Python path is useless as Python does not know how to use it
        if ( templatesSearchDir.startsWith( QString::fromUtf8(":/Resources") ) ) {
            continue;
        }
        QDir d(templatesSearchDir);
        operateOnPathRecursive(&addToPythonPathFunctor, d);
    }

    ///Also import Pyside.QtCore and Pyside.QtGui (the later only in non background mode)
    {
        std::string s;
        if (SHIBOKEN_MAJOR_VERSION == 2) {
            s = "import PySide2\nimport PySide2.QtCore as QtCore";
        } else {
            s = "import PySide\nimport PySide.QtCore as QtCore";
        }
        bool ok  = NATRON_PYTHON_NAMESPACE::interpretPythonScript(s, &err, 0);
        if (!ok) {
            QString message = tr("Failed to import PySide.QtCore, make sure it is bundled with your Natron installation "
                                     "or reachable through the Python path. "
                                     "Note that Natron disables usage "
                                 "of site-packages).");
            std::cerr << message.toStdString() << std::endl;
            appPTR->writeToErrorLog_mt_safe(QLatin1String("PySide.QtCore"), QDateTime::currentDateTime(), message);
        }
    }

    if ( !isBackground() ) {
        std::string s;
        if (SHIBOKEN_MAJOR_VERSION == 2) {
            s = "import PySide2.QtGui as QtGui";
        } else {
            s = "import PySide.QtGui as QtGui";
        }
        bool ok  = NATRON_PYTHON_NAMESPACE::interpretPythonScript(s, &err, 0);
        if (!ok) {
            QString message = tr("Failed to import PySide.QtGui");
            std::cerr << message.toStdString() << std::endl;
            appPTR->writeToErrorLog_mt_safe(QLatin1String("PySide.QtGui"), QDateTime::currentDateTime(), message);
        }
    }


    QStringList foundInit;
    QStringList foundInitGui;
    Q_FOREACH(const QString &templatesSearchDir, templatesSearchPath) {
        QDir d(templatesSearchDir);

        findAllScriptsRecursive(d, allPlugins, &foundInit, &foundInitGui);
    }
    if ( foundInit.isEmpty() ) {
        QString message = tr("Info: init.py script not loaded (this is not an error)");
        appPTR->setLoadingStatus(message);
        if ( !appPTR->isBackground() ) {
            std::cout << message.toStdString() << std::endl;
        }
    } else {
        Q_FOREACH(const QString &found, foundInit) {
            QString message = tr("Info: init.py script found and loaded at %1").arg(found);

            appPTR->setLoadingStatus(message);
            if ( !appPTR->isBackground() ) {
                std::cout << message.toStdString() << std::endl;
            }
        }
    }

    if ( !appPTR->isBackground() ) {
        if ( foundInitGui.isEmpty() ) {
            QString message = tr("Info: initGui.py script not loaded (this is not an error)");
            appPTR->setLoadingStatus(message);
            if ( !appPTR->isBackground() ) {
                std::cout << message.toStdString() << std::endl;
            }
        } else {
            Q_FOREACH(const QString &found, foundInitGui) {
                QString message = tr("Info: initGui.py script found and loaded at %1").arg(found);

                appPTR->setLoadingStatus(message);
                if ( !appPTR->isBackground() ) {
                    std::cout << message.toStdString() << std::endl;
                }
            }
        }
    }

    // Now that init.py and initGui.py have run, we need to set the search path again for the PyPlug
    // as the user might have called appendToNatronPath

    QStringList newTemplatesSearchPath = getAllNonOFXPluginsPaths();
    {
        QStringList diffSearch;
        Q_FOREACH(const QString &newTemplatesSearchDir, newTemplatesSearchPath) {
            if ( !templatesSearchPath.contains(newTemplatesSearchDir) ) {
                diffSearch.push_back(newTemplatesSearchDir);
            }
        }

        //Add only paths that did not exist so far
        Q_FOREACH(const QString &diffDir, diffSearch) {
            QDir d(diffDir);

            operateOnPathRecursive(&addToPythonPathFunctor, d);
        }
    }

    // Load deprecated PyPlugs encoded using Python scripts
    Q_FOREACH(const QString &plugin, allPlugins) {
        QString moduleName = plugin;
        QString modulePath;
        int lastDot = moduleName.lastIndexOf( QChar::fromLatin1('.') );

        if (lastDot != -1) {
            moduleName = moduleName.left(lastDot);
        }
        int lastSlash = moduleName.lastIndexOf( QChar::fromLatin1('/') );
        if (lastSlash != -1) {
            modulePath = moduleName.mid(0, lastSlash + 1);
            moduleName = moduleName.remove(0, lastSlash + 1);
        }

        std::string pluginLabel, pluginID, pluginGrouping, iconFilePath, pluginDescription, pluginPath;

        {
            // Open the file and check for a line that imports NatronGui, if so do not attempt to load the script.
            QFile file(plugin);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            QTextStream ts(&file);
            bool gotNatronGuiImport = false;
            bool isPyPlug = false;
            while (!ts.atEnd()) {
                QString line = ts.readLine();
                if (line.startsWith(QString::fromUtf8("import %1").arg(QLatin1String(NATRON_GUI_PYTHON_MODULE_NAME))) ||
                    line.startsWith(QString::fromUtf8("from %1 import").arg(QLatin1String(NATRON_GUI_PYTHON_MODULE_NAME)))) {
                    gotNatronGuiImport = true;
                }
                if (line.startsWith(QString::fromUtf8("# This file was automatically generated by Natron PyPlug exporter"))) {
                    isPyPlug = true;
                }

            }
            if (appPTR->isBackground() && gotNatronGuiImport) {
                continue;
            }
            if (!isPyPlug) {
                continue;
            }
        }

        unsigned int version;
        bool isToolset;
        bool gotInfos = NATRON_PYTHON_NAMESPACE::getGroupInfos(moduleName.toStdString(), &pluginID, &pluginLabel, &iconFilePath, &pluginGrouping, &pluginDescription, &pluginPath, &isToolset, &version);


        if (!gotInfos) {
            continue;
        }


        std::vector<std::string> grouping;
        boost::split(grouping, pluginGrouping, boost::is_any_of("/"));

        PluginPtr p = Plugin::create(0, pluginID, pluginLabel, version, 0, grouping);
        p->setProperty<std::string>(kNatronPluginPropPyPlugScriptAbsoluteFilePath, plugin.toStdString());
        p->setProperty<bool>(kNatronPluginPropPyPlugIsToolset, isToolset);
        p->setProperty<std::string>(kNatronPluginPropDescription, pluginDescription);
        p->setProperty<std::string>(kNatronPluginPropIconFilePath, iconFilePath);
        p->setProperty<bool>(kNatronPluginPropPyPlugIsPythonScript, true);
        p->setProperty<std::string>(kNatronPluginPropResourcesPath, modulePath.toStdString());
        //p->setProperty<bool>(kNatronPluginPropDescriptionIsMarkdown, false);
        //p->setProperty<int>(kNatronPluginPropShortcut, obj.presetSymbol, 0);
        //p->setProperty<int>(kNatronPluginPropShortcut, obj.presetModifiers, 1);
        registerPlugin(p);

    }
} // AppManager::loadPythonGroups

void
AppManager::registerPlugin(const PluginPtr& plugin)
{

    std::string pluginID = plugin->getPluginID();
    if ( ReadNode::isBundledReader( pluginID ) ||
         WriteNode::isBundledWriter( pluginID ) ) {
        plugin->setProperty<bool>(kNatronPluginPropIsInternalOnly, true);
    }

    PluginsMap::iterator found = _imp->_plugins.find(pluginID);
    if ( found != _imp->_plugins.end() ) {
        found->second.insert(plugin);
    } else {
        PluginMajorsOrdered &set = _imp->_plugins[pluginID];
        set.insert(plugin);
    }

}

Format
AppManager::findExistingFormat(int w,
                               int h,
                               double par) const
{
    for (U32 i = 0; i < _imp->_formats.size(); ++i) {
        const Format& frmt = _imp->_formats[i];
        if ( (frmt.width() == w) && (frmt.height() == h) && (frmt.getPixelAspectRatio() == par) ) {
            return frmt;
        }
    }

    return Format();
}

void
AppManager::setAsTopLevelInstance(int appID)
{
    QMutexLocker k(&_imp->_appInstancesMutex);

    if (_imp->_topLevelInstanceID == appID) {
        return;
    }
    _imp->_topLevelInstanceID = appID;
    for (AppInstanceVec::iterator it = _imp->_appInstances.begin();
         it != _imp->_appInstances.end();
         ++it) {
        if ( (*it)->getAppID() != _imp->_topLevelInstanceID ) {
            if ( !isBackground() ) {
                (*it)->disconnectViewersFromViewerCache();
            }
        } else {
            if ( !isBackground() ) {
                (*it)->connectViewersToViewerCache();
                setOFXHostHandle( (*it)->getOfxHostOSHandle() );
            }
        }
    }
}

void
AppManager::setOFXHostHandle(void* handle)
{
    _imp->ofxHost->setOfxHostOSHandle(handle);
}

void
AppManager::clearExceedingEntriesFromNodeCache()
{
    _imp->_nodeCache->clearExceedingEntries();
}

const PluginsMap&
AppManager::getPluginsList() const
{
    return _imp->_plugins;
}


const std::vector<Format> &
AppManager::getFormats() const
{
    return _imp->_formats;
}

const KnobFactory &
AppManager::getKnobFactory() const
{
    return *(_imp->_knobFactory);
}

PluginPtr
AppManager::getPluginBinaryFromOldID(const QString & pluginId,
                                     int majorVersion,
                                     int minorVersion,
                                     bool caseSensitive) const
{
    std::map<int, PluginPtr> matches;

    if ( pluginId == QString::fromUtf8("Viewer") ) {
        return _imp->findPluginById(PLUGINID_NATRON_VIEWER_GROUP, majorVersion, minorVersion);
    } else if ( pluginId == QString::fromUtf8("Dot") ) {
        return _imp->findPluginById(PLUGINID_NATRON_DOT, majorVersion, minorVersion );
    } else if ( pluginId == QString::fromUtf8("DiskCache") ) {
        return _imp->findPluginById(PLUGINID_NATRON_DISKCACHE, majorVersion, minorVersion);
    } else if ( pluginId == QString::fromUtf8("Backdrop") ) { // DO NOT change the capitalization, even if it's wrong
        return _imp->findPluginById(PLUGINID_NATRON_BACKDROP, majorVersion, minorVersion);
    } else if ( pluginId == QString::fromUtf8("RotoOFX  [Draw]") ) {
        return _imp->findPluginById(PLUGINID_NATRON_ROTO, majorVersion, minorVersion);
    } else if ( ( caseSensitive && ( pluginId == QString::fromUtf8(PLUGINID_OFX_ROTO) ) ) || ( !caseSensitive && ( pluginId == QString::fromUtf8(PLUGINID_OFX_ROTO).toLower() ) ) )  {
        return _imp->findPluginById(PLUGINID_NATRON_ROTO, majorVersion, minorVersion);
    }

    ///Try remapping these ids to old ids we had in Natron < 1.0 for backward-compat
    for (PluginsMap::const_iterator it = _imp->_plugins.begin(); it != _imp->_plugins.end(); ++it) {
        assert( !it->second.empty() );
        PluginMajorsOrdered::const_iterator it2 = it->second.begin();
        std::string friendlyLabel = (*it2)->getPluginLabel();
        std::string grouping0 = (*it2)->getProperty<std::string>(kNatronPluginPropGrouping, 0);
        friendlyLabel.append("  [" + grouping0 + "]");

        if (friendlyLabel == pluginId.toStdString()) {
            if (majorVersion == -1) {
                // -1 means we want to load the highest version existing
                return *( it->second.rbegin() );
            }

            //Look for the exact version
            for (; it2 != it->second.end(); ++it2) {
                if ( (*it2)->getProperty<unsigned int>(kNatronPluginPropVersion, 0) == (unsigned int)majorVersion ) {
                    return *it2;
                }
            }

            ///Could not find the exact version... let's just use the highest version found
            return *( it->second.rbegin() );
        }
    }

    return PluginPtr();
}

PluginPtr
AppManager::getPluginBinary(const QString & pluginId,
                            int majorVersion,
                            int /*minorVersion*/,
                            bool caseSensitivePluginSearch) const
{
    PluginsMap::const_iterator foundID = _imp->_plugins.end();

    for (PluginsMap::const_iterator it = _imp->_plugins.begin(); it != _imp->_plugins.end(); ++it) {
        QString pID = QString::fromUtf8( it->first.c_str() );
        if ( !caseSensitivePluginSearch &&
             !pluginId.startsWith( QString::fromUtf8(NATRON_ORGANIZATION_DOMAIN_TOPLEVEL "." NATRON_ORGANIZATION_DOMAIN_SUB ".built-in.") ) ) {
            QString lowerCase = pID.toLower();
            if (lowerCase == pluginId) {
                foundID = it;
                break;
            }
        }

        if (pID == pluginId) {
            foundID = it;
            break;
        }
    }


    if ( foundID != _imp->_plugins.end() ) {
        assert( !foundID->second.empty() );

        if (majorVersion == -1) {
            // -1 means we want to load the highest version existing
            return *foundID->second.rbegin();
        }

        ///Try to find the exact version
        for (PluginMajorsOrdered::const_iterator it = foundID->second.begin(); it != foundID->second.end(); ++it) {
            if ( ( (*it)->getProperty<unsigned int>(kNatronPluginPropVersion, 0) == (unsigned int)majorVersion ) ) {
                return *it;
            }
        }

        ///Could not find the exact version... let's just use the highest version found
        return *foundID->second.rbegin();
    }
    QString exc = QString::fromUtf8("Couldn't find a plugin attached to the ID %1, with a major version of %2")
                  .arg(pluginId)
                  .arg(majorVersion);

    throw std::invalid_argument( exc.toStdString() );
    //return PluginPtr();
}


NodePtr
AppManager::createNodeForProjectLoading(const SERIALIZATION_NAMESPACE::NodeSerializationPtr& serialization, const NodeCollectionPtr& group)
{

    NodePtr retNode = group->getNodeByName(serialization->_nodeScriptName);

    // When loading a group, if a node with the same name and plug-in ID already exists, return it
    if (retNode && retNode->getPluginID() == serialization->_pluginID) {
        return retNode;
    }
    {
        CreateNodeArgsPtr args(CreateNodeArgs::create(serialization->_pluginID, group));
        args->setProperty<int>(kCreateNodeArgsPropPluginVersion, serialization->_pluginMajorVersion, 0);
        args->setProperty<int>(kCreateNodeArgsPropPluginVersion, serialization->_pluginMinorVersion, 1);
        args->setProperty<SERIALIZATION_NAMESPACE::NodeSerializationPtr >(kCreateNodeArgsPropNodeSerialization, serialization);
        args->setProperty<bool>(kCreateNodeArgsPropSilent, true);
        args->setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        args->setProperty<bool>(kCreateNodeArgsPropAllowNonUserCreatablePlugins, true); // also load deprecated plugins
        retNode =  group->getApplication()->createNode(args);
    }
    if (retNode) {
        return retNode;
    }
    
    // If the node could not be created, make a Stub node
    {
        CreateNodeArgsPtr args(CreateNodeArgs::create(PLUGINID_NATRON_STUB, group));

        std::stringstream ss;
        try {
            SERIALIZATION_NAMESPACE::write(ss, *serialization, std::string());
        } catch (...) {
            return retNode;
        }
        
        args->addParamDefaultValue<std::string>(kStubNodeParamSerialization, ss.str());
        args->setProperty<bool>(kCreateNodeArgsPropSilent, true); // also load deprecated plugins
        args->setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        args->setProperty<bool>(kCreateNodeArgsPropAllowNonUserCreatablePlugins, true);
        args->setProperty<std::string>(kCreateNodeArgsPropNodeInitialName, serialization->_nodeScriptName);
        retNode = group->getApplication()->createNode(args);

    }
    return retNode;
}

void
AppManager::removeFromNodeCache(const ImagePtr & image)
{
    _imp->_nodeCache->removeEntry(image);
}

void
AppManager::removeFromViewerCache(const FrameEntryPtr & texture)
{
    _imp->_viewerCache->removeEntry(texture);
}

void
AppManager::removeFromNodeCache(U64 hash)
{
    _imp->_nodeCache->removeEntry(hash);
}

void
AppManager::removeFromViewerCache(U64 hash)
{
    _imp->_viewerCache->removeEntry(hash);
}


void
AppManager::removeAllCacheEntriesForPlugin(const std::string& pluginID)
{
    _imp->_nodeCache->removeAllEntriesForPluginPublic(pluginID, false);
    _imp->_diskCache->removeAllEntriesForPluginPublic(pluginID, false);
    _imp->_viewerCache->removeAllEntriesForPluginPublic(pluginID, false);
}

void
AppManager::queueEntriesForDeletion(const std::list<ImagePtr>& images)
{
    _imp->_nodeCache->appendToQueue(images);
}

void
AppManager::queueEntriesForDeletion(const std::list<FrameEntryPtr>& images)
{
    _imp->_viewerCache->appendToQueue(images);
}

void
AppManager::printCacheMemoryStats() const
{
    appPTR->clearErrorLog_mt_safe();
    std::map<std::string, CacheEntryReportInfo> infos;

    {
        // Cache entries for the viewer cache don't have a plug-in ID since this is the only plug-in using it
        std::map<std::string, CacheEntryReportInfo> viewerInfos;
        _imp->_viewerCache->getMemoryStats(&viewerInfos);

        CacheEntryReportInfo& data = infos[PLUGINID_NATRON_VIEWER_INTERNAL];
        for (std::map<std::string, CacheEntryReportInfo>::iterator it = viewerInfos.begin(); it!=viewerInfos.end(); ++it) {
            data.diskBytes += it->second.diskBytes;
            data.ramBytes += it->second.ramBytes;
        }
    }
    {
        _imp->_nodeCache->getMemoryStats(&infos);
    }
    {
        _imp->_diskCache->getMemoryStats(&infos);
    }

    QString reportStr;
    std::size_t totalDisk = 0;
    std::size_t totalRam = 0;
    reportStr += QLatin1String("\n");
    if (!infos.empty()) {
        for (std::map<std::string, CacheEntryReportInfo>::iterator it = infos.begin(); it!= infos.end(); ++it) {
            if (it->second.ramBytes == 0 && it->second.diskBytes == 0) {
                continue;
            }
            totalRam += it->second.ramBytes;
            totalDisk += it->second.diskBytes;

            reportStr += QString::fromUtf8(it->first.c_str());
            reportStr += QLatin1String("--> ");
            reportStr += QLatin1String("RAM: ");
            reportStr += printAsRAM(it->second.ramBytes);
            reportStr += QLatin1String(" Disk: ");
            reportStr += printAsRAM(it->second.diskBytes);
            reportStr += QLatin1String("\n");
        }
        reportStr += QLatin1String("-------------------------------\n");
    }
    reportStr += tr("Total");
    reportStr += QLatin1String("--> ");
    reportStr += QLatin1String("RAM: ");
    reportStr += printAsRAM(totalRam);
    reportStr += QLatin1String(" Disk: ");
    reportStr += printAsRAM(totalDisk);


    appPTR->writeToErrorLog_mt_safe(tr("Cache Report"), QDateTime::currentDateTime(), reportStr);

    appPTR->showErrorLog();
}



const QString &
AppManager::getApplicationBinaryPath() const
{
    return _imp->_binaryPath;
}

void
AppManager::setNumberOfThreads(int threadsNb)
{
    if (_imp->_settings) {
        _imp->_settings->setNumberOfThreads(threadsNb);
    }
}

bool
AppManager::getImage(const ImageKey & key,
                     std::list<ImagePtr >* returnValue) const
{
    return _imp->_nodeCache->get(key, returnValue);
}

bool
AppManager::getImageOrCreate(const ImageKey & key,
                             const ImageParamsPtr& params,
                             ImageLocker* locker,
                             ImagePtr* returnValue) const
{
    return _imp->_nodeCache->getOrCreate(key, params, locker, returnValue);
}

bool
AppManager::getImage_diskCache(const ImageKey & key,
                               std::list<ImagePtr >* returnValue) const
{
    return _imp->_diskCache->get(key, returnValue);
}

bool
AppManager::getImageOrCreate_diskCache(const ImageKey & key,
                                       const ImageParamsPtr& params,
                                       ImagePtr* returnValue) const
{
    return _imp->_diskCache->getOrCreate(key, params, 0, returnValue);
}

bool
AppManager::getTexture(const FrameKey & key,
                       std::list<FrameEntryPtr>* returnValue) const
{
    std::list<FrameEntryPtr > retList;
    bool ret =  _imp->_viewerCache->get(key, &retList);

    *returnValue = retList;

    return ret;
}

bool
AppManager::getTextureOrCreate(const FrameKey & key,
                               const boost::shared_ptr<FrameParams>& params,
                               FrameEntryLocker* locker,
                               FrameEntryPtr* returnValue) const
{
    return _imp->_viewerCache->getOrCreate(key, params, locker, returnValue);
}

bool
AppManager::isAggressiveCachingEnabled() const
{
    return _imp->_settings->isAggressiveCachingEnabled();
}

U64
AppManager::getCachesTotalMemorySize() const
{
    return  _imp->_nodeCache->getMemoryCacheSize();
}

U64
AppManager::getCachesTotalDiskSize() const
{
    return  _imp->_diskCache->getDiskCacheSize() + _imp->_viewerCache->getDiskCacheSize();
}

boost::shared_ptr<CacheSignalEmitter>
AppManager::getOrActivateViewerCacheSignalEmitter() const
{
    return _imp->_viewerCache->activateSignalEmitter();
}

SettingsPtr AppManager::getCurrentSettings() const
{
    return _imp->_settings;
}

void
AppManager::setLoadingStatus(const QString & str)
{
    if ( isLoaded() ) {
        return;
    }
    std::cout << str.toStdString() << std::endl;
}

AppInstancePtr
AppManager::makeNewInstance(int appID) const
{
    return AppInstance::create(appID);
}

void
AppManager::registerEngineMetaTypes() const
{
    qRegisterMetaType<Variant>("Variant");
    qRegisterMetaType<Format>("Format");
    qRegisterMetaType<SequenceTime>("SequenceTime");
    qRegisterMetaType<StandardButtons>("StandardButtons");
    qRegisterMetaType<RectI>("RectI");
    qRegisterMetaType<RectD>("RectD");
    qRegisterMetaType<RenderStatsPtr>("RenderStatsPtr");
    qRegisterMetaType<RenderStatsMap>("RenderStatsMap");
    qRegisterMetaType<ViewIdx>("ViewIdx");
    qRegisterMetaType<ViewSetSpec>("ViewSetSpec");
    qRegisterMetaType<ViewGetSpec>("ViewGetSpec");
    qRegisterMetaType<NodePtr >("NodePtr");
    qRegisterMetaType<ViewerInstancePtr >("ViewerInstancePtr");
    qRegisterMetaType<std::list<double> >("std::list<double>");
    qRegisterMetaType<DimIdx>("DimIdx");
    qRegisterMetaType<DimSpec>("DimSpec");
    qRegisterMetaType<ValueChangedReturnCodeEnum>("ValueChangedReturnCodeEnum");
    qRegisterMetaType<ValueChangedReasonEnum>("ValueChangedReasonEnum");
    qRegisterMetaType<DimensionViewPair>("DimensionViewPair");
    qRegisterMetaType<PerDimViewVariantMap>("PerDimViewVariantMap");
#if QT_VERSION < 0x050000
    qRegisterMetaType<QAbstractSocket::SocketState>("SocketState");
#endif
}

void
AppManager::setDiskCacheLocation(const QString& path)
{
    QDir d(path);
    QMutexLocker k(&_imp->diskCachesLocationMutex);

    if ( d.exists() && !path.isEmpty() ) {
        _imp->diskCachesLocation = path;
    } else {
        _imp->diskCachesLocation = StandardPaths::writableLocation(StandardPaths::eStandardLocationCache);
    }
}

const QString&
AppManager::getDiskCacheLocation() const
{
    QMutexLocker k(&_imp->diskCachesLocationMutex);

    return _imp->diskCachesLocation;
}

bool
AppManager::isNCacheFilesOpenedCapped() const
{
    QMutexLocker l(&_imp->currentCacheFilesCountMutex);

    return _imp->currentCacheFilesCount >= _imp->maxCacheFiles;
}

size_t
AppManager::getNCacheFilesOpened() const
{
    QMutexLocker l(&_imp->currentCacheFilesCountMutex);

    return _imp->currentCacheFilesCount;
}

void
AppManager::increaseNCacheFilesOpened()
{
    QMutexLocker l(&_imp->currentCacheFilesCountMutex);

    ++_imp->currentCacheFilesCount;
#ifdef DEBUG
    if (_imp->currentCacheFilesCount > _imp->maxCacheFiles) {
        qDebug() << "Cache has more files opened than the limit allowed:" << _imp->currentCacheFilesCount << '/' << _imp->maxCacheFiles;
    }
#endif
#ifdef NATRON_DEBUG_CACHE
    qDebug() << "N Cache Files Opened:" << _imp->currentCacheFilesCount;
#endif
}

void
AppManager::decreaseNCacheFilesOpened()
{
    QMutexLocker l(&_imp->currentCacheFilesCountMutex);

    --_imp->currentCacheFilesCount;
#ifdef NATRON_DEBUG_CACHE
    qDebug() << "NFiles Opened:" << _imp->currentCacheFilesCount;
#endif
}

void
AppManager::onMaxPanelsOpenedChanged(int maxPanels)
{
    AppInstanceVec copy;
    {
        QMutexLocker k(&_imp->_appInstancesMutex);
        copy = _imp->_appInstances;
    }

    for (AppInstanceVec::iterator it = copy.begin(); it != copy.end(); ++it) {
        (*it)->onMaxPanelsOpenedChanged(maxPanels);
    }
}

void
AppManager::onQueueRendersChanged(bool queuingEnabled)
{
    AppInstanceVec copy;
    {
        QMutexLocker k(&_imp->_appInstancesMutex);
        copy = _imp->_appInstances;
    }

    for (AppInstanceVec::iterator it = copy.begin(); it != copy.end(); ++it) {
        (*it)->onRenderQueuingChanged(queuingEnabled);
    }
}

int
AppManager::exec()
{
    return qApp->exec();
}

void
AppManager::onNodeMemoryRegistered(qint64 mem)
{
    ///runs only in the main thread
    assert( QThread::currentThread() == qApp->thread() );

    if ( ( (qint64)_imp->_nodesGlobalMemoryUse + mem ) < 0 ) {
        qDebug() << "Memory underflow...a node is trying to release more memory than it registered.";
        _imp->_nodesGlobalMemoryUse = 0;

        return;
    }

    _imp->_nodesGlobalMemoryUse += mem;
}

qint64
AppManager::getTotalNodesMemoryRegistered() const
{
    assert( QThread::currentThread() == qApp->thread() );

    return _imp->_nodesGlobalMemoryUse;
}

void
AppManager::getErrorLog_mt_safe(std::list<LogEntry>* entries) const
{
    QMutexLocker l(&_imp->errorLogMutex);
    *entries = _imp->errorLog;
}

void
AppManager::writeToErrorLog_mt_safe(const QString& context,
                                    const QDateTime& date,
                                    const QString & str,
                                    bool isHtml,
                                    const LogEntry::LogEntryColor& color)
{
    QMutexLocker l(&_imp->errorLogMutex);
    LogEntry e;
    e.context = context;
    e.date = date;
    e.message = str;
    e.isHtml = isHtml;
    e.color = color;
    _imp->errorLog.push_back(e);
}

void
AppManager::showErrorLog()
{
    std::list<LogEntry> log;
    getErrorLog_mt_safe(&log);
    for (std::list<LogEntry>::iterator it = log.begin(); it != log.end(); ++it) {
        // only print time - QTime.toString() uses the system locale, that's not what we want
        std::cout << QString::fromUtf8("[%2] %1: %3")
                     .arg(it->context)
                     .arg( QLocale().toString( it->date.time(), QString::fromUtf8("HH:mm:ss.zzz")) )
                     .arg(it->message).toStdString() << std::endl;
    }
}

void
AppManager::clearErrorLog_mt_safe()
{
    QMutexLocker l(&_imp->errorLogMutex);

    _imp->errorLog.clear();
}

void
AppManager::exitApp(bool /*warnUserForSave*/)
{
    const AppInstanceVec & instances = getAppInstances();

    for (AppInstanceVec::const_iterator it = instances.begin(); it != instances.end(); ++it) {
        (*it)->quitNow();
    }
}

#ifdef Q_OS_UNIX
QString
AppManager::qt_tildeExpansion(const QString &path,
                              bool *expanded)
{
    if (expanded != 0) {
        *expanded = false;
    }
    if ( !path.startsWith( QLatin1Char('~') ) ) {
        return path;
    }
    QString ret = path;
    QStringList tokens = ret.split( QDir::separator() );
    if ( tokens.first() == QLatin1String("~") ) {
        ret.replace( 0, 1, QDir::homePath() );
    } /*else {
         QString userName = tokens.first();
         userName.remove(0, 1);

         const QString homePath = QString::fro#if defined(Q_OS_VXWORKS)
         const QString homePath = QDir::homePath();
         #elif defined(_POSIX_THREAD_SAFE_FUNCTIONS) && !defined(Q_OS_OPENBSD)
         passwd pw;
         passwd *tmpPw;
         char buf[200];
         const int bufSize = sizeof(buf);
         int err = 0;
         #if defined(Q_OS_SOLARIS) && (_POSIX_C_SOURCE - 0 < 199506L)
         tmpPw = getpwnam_r(userName.toLocal8Bit().constData(), &pw, buf, bufSize);
         #else
         err = getpwnam_r(userName.toLocal8Bit().constData(), &pw, buf, bufSize, &tmpPw);
         #endif
         if (err || !tmpPw)
         return ret;mLocal8Bit(pw.pw_dir);
         #else
         passwd *pw = getpwnam(userName.toLocal8Bit().constData());
         if (!pw)
         return ret;
         const QString homePath = QString::fromLocal8Bit(pw->pw_dir);
         #endif
         ret.replace(0, tokens.first().length(), homePath);
         }*/
    if (expanded != 0) {
        *expanded = true;
    }

    return ret;
}

#endif

bool
AppManager::isNodeCacheAlmostFull() const
{
    std::size_t nodeCacheSize = _imp->_nodeCache->getMemoryCacheSize();
    std::size_t nodeMaxCacheSize = _imp->_nodeCache->getMaximumMemorySize();

    if (nodeMaxCacheSize == 0) {
        return true;
    }

    if ( (double)nodeCacheSize / nodeMaxCacheSize >= NATRON_CACHE_LIMIT_PERCENT ) {
        return true;
    } else {
        return false;
    }
}

void
AppManager::checkCacheFreeMemoryIsGoodEnough()
{
    ///Before allocating the memory check that there's enough space to fit in memory
    size_t systemRAMToKeepFree = getSystemTotalRAM() * appPTR->getCurrentSettings()->getUnreachableRamPercent();
    size_t totalFreeRAM = getAmountFreePhysicalRAM();

    while (totalFreeRAM <= systemRAMToKeepFree) {
#ifdef NATRON_DEBUG_CACHE
        qDebug() << "Total system free RAM is below the threshold:" << printAsRAM(totalFreeRAM)
        << ", clearing least recently used NodeCache image...";
#endif
        if ( !_imp->_nodeCache->evictLRUInMemoryEntry() ) {
            break;
        }


        totalFreeRAM = getAmountFreePhysicalRAM();
    }
}

void
AppManager::onOCIOConfigPathChanged(const std::string& path)
{
    _imp->currentOCIOConfigPath = path;

    AppInstanceVec copy;
    {
        QMutexLocker k(&_imp->_appInstancesMutex);
        copy = _imp->_appInstances;
    }

    for (AppInstanceVec::iterator it = copy.begin(); it != copy.end(); ++it) {
        (*it)->onOCIOConfigPathChanged(path);
    }
}

const std::string&
AppManager::getOCIOConfigPath() const
{
    return _imp->currentOCIOConfigPath;
}

void
AppManager::setNThreadsToRender(int nThreads)
{
    QMutexLocker l(&_imp->nThreadsMutex);

    _imp->nThreadsToRender = nThreads;
}

void
AppManager::getNThreadsSettings(int* nThreadsToRender,
                                int* nThreadsPerEffect) const
{
    QMutexLocker l(&_imp->nThreadsMutex);

    *nThreadsToRender = _imp->nThreadsToRender;
    *nThreadsPerEffect = _imp->nThreadsPerEffect;
}

void
AppManager::setNThreadsPerEffect(int nThreadsPerEffect)
{
    QMutexLocker l(&_imp->nThreadsMutex);

    _imp->nThreadsPerEffect = nThreadsPerEffect;
}

void
AppManager::setUseThreadPool(bool useThreadPool)
{
    QMutexLocker l(&_imp->nThreadsMutex);

    _imp->useThreadPool = useThreadPool;
}

bool
AppManager::getUseThreadPool() const
{
    QMutexLocker l(&_imp->nThreadsMutex);

    return _imp->useThreadPool;
}

void
AppManager::fetchAndAddNRunningThreads(int nThreads)
{
    _imp->runningThreadsCount.fetchAndAddRelaxed(nThreads);
}

int
AppManager::getNRunningThreads() const
{
    return (int)_imp->runningThreadsCount;
}

void
AppManager::setThreadAsActionCaller(OfxImageEffectInstance* instance,
                                    bool actionCaller)
{
    _imp->ofxHost->setThreadAsActionCaller(instance, actionCaller);
}

void
AppManager::requestOFXDIalogOnMainThread(OfxImageEffectInstance* instance,
                                         void* instanceData)
{
    if ( QThread::currentThread() == qApp->thread() ) {
        onOFXDialogOnMainThreadReceived(instance, instanceData);
    } else {
        Q_EMIT s_requestOFXDialogOnMainThread(instance, instanceData);
    }
}

void
AppManager::onOFXDialogOnMainThreadReceived(OfxImageEffectInstance* instance,
                                            void* instanceData)
{
    assert( QThread::currentThread() == qApp->thread() );
    if (!instance) {
        // instance may be NULL if using OfxDialogSuiteV1
        OfxHost::OfxHostDataTLSPtr tls = _imp->ofxHost->getTLSData();
        instance = tls->lastEffectCallingMainEntry;
    } else {
#ifdef DEBUG
        OfxHost::OfxHostDataTLSPtr tls = _imp->ofxHost->getTLSData();
        assert(instance == tls->lastEffectCallingMainEntry);
#endif
    }
#ifdef OFX_SUPPORTS_DIALOG
    if (instance) {
        instance->dialog(instanceData);
    }
#else
    Q_UNUSED(instanceData);
#endif
}

std::list<std::string>
AppManager::getPluginIDs() const
{
    std::list<std::string> ret;

    for (PluginsMap::const_iterator it = _imp->_plugins.begin(); it != _imp->_plugins.end(); ++it) {
        assert( !it->second.empty() );
        ret.push_back(it->first);
    }

    return ret;
}

std::list<std::string>
AppManager::getPluginIDs(const std::string& filter)
{
    QString qFilter = QString::fromUtf8( filter.c_str() );
    std::list<std::string> ret;

    for (PluginsMap::const_iterator it = _imp->_plugins.begin(); it != _imp->_plugins.end(); ++it) {
        assert( !it->second.empty() );

        QString pluginID = QString::fromUtf8( it->first.c_str() );
        if ( pluginID.contains(qFilter, Qt::CaseInsensitive) ) {
            ret.push_back(it->first);
        }
    }

    return ret;
}


std::string
NATRON_PYTHON_NAMESPACE::PyStringToStdString(PyObject* obj)
{
    std::string ret;

    if ( PyString_Check(obj) ) {
        char* buf = PyString_AsString(obj);
        if (buf) {
            ret += std::string(buf);
        }
    } else if ( PyUnicode_Check(obj) ) {
        /*PyObject * temp_bytes = PyUnicode_AsEncodedString(obj, "ASCII", "strict"); // Owned reference
           if (temp_bytes != NULL) {
           char* cstr = PyBytes_AS_STRING(temp_bytes); // Borrowed pointer
           ret.append(cstr);
           Py_DECREF(temp_bytes);
           }*/
        PyObject* utf8pyobj = PyUnicode_AsUTF8String(obj); // newRef
        if (utf8pyobj) {
            char* cstr = PyBytes_AS_STRING(utf8pyobj); // Borrowed pointer
            ret.append(cstr);
            Py_DECREF(utf8pyobj);
        }
    } else if ( PyBytes_Check(obj) ) {
        char* cstr = PyBytes_AS_STRING(obj); // Borrowed pointer
        ret.append(cstr);
    }

    return ret;
}

void
AppManager::initPython()
{
#ifdef NATRON_RUN_WITHOUT_PYTHON

    return;
#endif
    //Disable user sites as they could conflict with Natron bundled packages.
    //If this is set, Python won’t add the user site-packages directory to sys.path.
    //See https://www.python.org/dev/peps/pep-0370/
    qputenv("PYTHONNOUSERSITE", "1");
    ++Py_NoUserSiteDirectory;

    //
    // set up paths, clear those that don't exist or are not valid
    //
    QString binPath = QCoreApplication::applicationDirPath();
    binPath = QDir::toNativeSeparators(binPath);
#ifdef __NATRON_WIN32__
    static std::string pythonHome = binPath.toStdString() + "\\.."; // must use static storage
    QString pyPathZip = QString::fromUtf8( (pythonHome + "\\lib\\python" NATRON_PY_VERSION_STRING_NO_DOT ".zip").c_str() );
    QString pyPath = QString::fromUtf8( (pythonHome +  "\\lib\\python" NATRON_PY_VERSION_STRING).c_str() );
    QString pluginPath = binPath + QString::fromUtf8("\\..\\Plugins");
#else
#  if defined(__NATRON_LINUX__)
    static std::string pythonHome = binPath.toStdString() + "/.."; // must use static storage
#  elif defined(__NATRON_OSX__)
    static std::string pythonHome = binPath.toStdString() + "/../Frameworks/Python.framework/Versions/" NATRON_PY_VERSION_STRING; // must use static storage
#  else
#    error "unsupported platform"
#  endif
    QString pyPathZip = QString::fromUtf8( (pythonHome + "/lib/python" NATRON_PY_VERSION_STRING_NO_DOT ".zip").c_str() );
    QString pyPath = QString::fromUtf8( (pythonHome + "/lib/python" NATRON_PY_VERSION_STRING).c_str() );
    QString pluginPath = binPath + QString::fromUtf8("/../Plugins");
#endif
    if ( !QFile( QDir::fromNativeSeparators(pyPathZip) ).exists() ) {
#     if defined(NATRON_CONFIG_SNAPSHOT) || defined(DEBUG)
        printf( "\"%s\" does not exist, not added to PYTHONPATH\n", pyPathZip.toStdString().c_str() );
#     endif
        pyPathZip.clear();
    }
    if ( !QDir( QDir::fromNativeSeparators(pyPath) ).exists() ) {
#     if defined(NATRON_CONFIG_SNAPSHOT) || defined(DEBUG)
        printf( "\"%s\" does not exist, not added to PYTHONPATH\n", pyPath.toStdString().c_str() );
#     endif
        pyPath.clear();
    }
    if ( !QDir( QDir::fromNativeSeparators(pluginPath) ).exists() ) {
#     if defined(NATRON_CONFIG_SNAPSHOT) || defined(DEBUG)
        printf( "\"%s\" does not exist, not added to PYTHONPATH\n", pluginPath.toStdString().c_str() );
#     endif
        pluginPath.clear();
    }
    // PYTHONHOME is really useful if there's a python inside it
    if ( pyPathZip.isEmpty() && pyPath.isEmpty() ) {
#     if defined(NATRON_CONFIG_SNAPSHOT) || defined(DEBUG)
        printf( "dir \"%s\" does not exist or does not contain lib/python*, not setting PYTHONHOME\n", pythonHome.c_str() );
#     endif
        pythonHome.clear();
    }
    /////////////////////////////////////////
    // Py_SetPythonHome
    /////////////////////////////////////////
    //
    // Must be done before Py_Initialize (see doc of Py_Initialize)
    //
    // The argument should point to a zero-terminated character string in static storage whose contents will not change for the duration of the program’s execution

    if ( !pythonHome.empty() ) {
#     if defined(NATRON_CONFIG_SNAPSHOT) || defined(DEBUG)
        printf( "Py_SetPythonHome(\"%s\")\n", pythonHome.c_str() );
#     endif
#     if PY_MAJOR_VERSION >= 3
        // Python 3
        static const std::wstring pythonHomeW = StrUtils::utf8_to_utf16(pythonHome); // must use static storage
        Py_SetPythonHome( const_cast<wchar_t*>( pythonHomeW.c_str() ) );
#     else
        // Python 2
        Py_SetPythonHome( const_cast<char*>( pythonHome.c_str() ) );
#     endif
    }

    /////////////////////////////////////////
    // PYTHONPATH and Py_SetPath
    /////////////////////////////////////////
    //
    // note: to check the python path of a python install, execute:
    // python -c 'import sys,pprint; pprint.pprint( sys.path )'
    //
    // to build the python27.zip, cd to lib/python2.7, and generate the pyo and the zip file using:
    //
    //  python -O -m compileall .
    //  zip -r ../python27.zip *.py* bsddb compiler ctypes curses distutils email encodings hotshot idlelib importlib json logging multiprocessing pydoc_data sqlite3 unittest wsgiref xml
    //
    QString pythonPath = QString::fromUtf8( qgetenv("PYTHONPATH") );
    //Add the Python distribution of Natron to the Python path

    QStringList toPrepend;
    if ( !pyPathZip.isEmpty() ) {
        toPrepend.append(pyPathZip);
    }
    if ( !pyPath.isEmpty() ) {
        toPrepend.append(pyPath);
    }
    if ( !pluginPath.isEmpty() ) {
        toPrepend.append(pluginPath);
    }

#if defined(__NATRON_OSX__) && defined DEBUG
    // in debug mode, also prepend the local PySide directory
    // homebrew's pyside directory
    toPrepend.append( QString::fromUtf8("/usr/local/Cellar/pyside/1.2.2_1/lib/python" NATRON_PY_VERSION_STRING "/site-packages") );
    // macport's pyside directory
    toPrepend.append( QString::fromUtf8("/opt/local/Library/Frameworks/Python.framework/Versions/" NATRON_PY_VERSION_STRING "/lib/python" NATRON_PY_VERSION_STRING "/site-packages") );
#endif

    if ( toPrepend.isEmpty() ) {
#     if defined(NATRON_CONFIG_SNAPSHOT) || defined(DEBUG)
        printf("PYTHONPATH not modified\n");
#     endif
    } else {
#     ifdef __NATRON_WIN32__
        QChar pathSep = QChar::fromLatin1(';');
#     else
        QChar pathSep = QChar::fromLatin1(':');
#     endif
        QString toPrependStr = toPrepend.join(pathSep);
        if (pythonPath.isEmpty()) {
            pythonPath = toPrependStr;
        } else {
            pythonPath = toPrependStr + pathSep + pythonPath;
        }
        // qputenv on minw will just call putenv, but we want to keep the utf16 info, so we need to call _wputenv
#     if 0//def __NATRON_WIN32__
        _wputenv_s(L"PYTHONPATH", StrUtils::utf8_to_utf16(pythonPath.toStdString()).c_str());
#     else
        std::string pythonPathString = pythonPath.toStdString();
        qputenv( "PYTHONPATH", pythonPathString.c_str() );
        //Py_SetPath( pythonPathString.c_str() ); // does not exist in Python 2
#     endif
#     if PY_MAJOR_VERSION >= 3
        std::wstring pythonPathString = StrUtils::utf8_to_utf16( pythonPath.toStdString() );
        Py_SetPath( pythonPathString.c_str() ); // argument is copied internally, no need to use static storage
#     endif
#     if defined(NATRON_CONFIG_SNAPSHOT) || defined(DEBUG)
        printf( "PYTHONPATH set to %s\n", pythonPath.toStdString().c_str() );
#     endif
    }

    /////////////////////////////////////////
    // Py_SetProgramName
    /////////////////////////////////////////
    //
    // Must be done before Py_Initialize (see doc of Py_Initialize)
    //
#if defined(NATRON_CONFIG_SNAPSHOT) || defined(DEBUG)
    printf( "Py_SetProgramName(\"%s\")\n", _imp->commandLineArgsUtf8[0] );
#endif
#if PY_MAJOR_VERSION >= 3
    // Python 3
    Py_SetProgramName(_imp->commandLineArgsWide[0]);
#else
    // Python 2
    Py_SetProgramName(_imp->commandLineArgsUtf8[0]);
#endif


    ///Must be called prior to Py_Initialize (calls PyImport_AppendInittab())
    initBuiltinPythonModules();

    //See https://developer.blender.org/T31507
    //Python will not load anything in site-packages if this is set
    //We are sure that nothing in system wide site-packages is loaded, for instance on OS X with Python installed
    //through macports on the system, the following printf show the following:

    /*Py_GetProgramName is /Applications/Natron.app/Contents/MacOS/Natron
       Py_GetPrefix is /Applications/Natron.app/Contents/MacOS/../Frameworks/Python.framework/Versions/2.7
       Py_GetExecPrefix is /Applications/Natron.app/Contents/MacOS/../Frameworks/Python.framework/Versions/2.7
       Py_GetProgramFullPath is /Applications/Natron.app/Contents/MacOS/Natron
       Py_GetPath is /Applications/Natron.app/Contents/MacOS/../Frameworks/Python.framework/Versions/2.7/lib/python2.7:/Applications/Natron.app/Contents/MacOS/../Plugins:/Applications/Natron.app/Contents/MacOS/../Frameworks/Python.framework/Versions/2.7/lib/python27.zip:/Applications/Natron.app/Contents/MacOS/../Frameworks/Python.framework/Versions/2.7/lib/python2.7/:/Applications/Natron.app/Contents/MacOS/../Frameworks/Python.framework/Versions/2.7/lib/python2.7/plat-darwin:/Applications/Natron.app/Contents/MacOS/../Frameworks/Python.framework/Versions/2.7/lib/python2.7/plat-mac:/Applications/Natron.app/Contents/MacOS/../Frameworks/Python.framework/Versions/2.7/lib/python2.7/plat-mac/lib-scriptpackages:/Applications/Natron.app/Contents/MacOS/../Frameworks/Python.framework/Versions/2.7/lib/python2.7/lib-tk:/Applications/Natron.app/Contents/MacOS/../Frameworks/Python.framework/Versions/2.7/lib/python2.7/lib-old:/Applications/Natron.app/Contents/MacOS/../Frameworks/Python.framework/Versions/2.7/lib/python2.7/lib-dynload
       Py_GetPythonHome is ../Frameworks/Python.framework/Versions/2.7/lib
       Python library is in /Applications/Natron.app/Contents/Frameworks/Python.framework/Versions/2.7/lib/python2.7/site-packages*/

    //Py_NoSiteFlag = 1;


    /////////////////////////////////////////
    // Py_Initialize
    /////////////////////////////////////////
    //
    // Initialize the Python interpreter. In an application embedding Python, this should be called before using any other Python/C API functions; with the exception of Py_SetProgramName(), Py_SetPythonHome() and Py_SetPath().
#if defined(NATRON_CONFIG_SNAPSHOT) || defined(DEBUG)
    printf("Py_Initialize()\n");
#endif
    Py_Initialize();
    // pythonHome must be const, so that the c_str() pointer is never invalidated

    /////////////////////////////////////////
    // PySys_SetArgv
    /////////////////////////////////////////
    //
#if PY_MAJOR_VERSION >= 3
    // Python 3
    PySys_SetArgv( argc, &_imp->args.front() ); /// relative module import
#else
    // Python 2
    PySys_SetArgv( _imp->commandLineArgsUtf8.size(), &_imp->commandLineArgsUtf8.front() ); /// relative module import
#endif

    _imp->mainModule = PyImport_ImportModule("__main__"); //create main module , new ref

    //See http://wiki.blender.org/index.php/Dev:2.4/Source/Python/API/Threads
    //Python releases the GIL every 100 virtual Python instructions, we do not want that to happen in the middle of an expression.
    //_PyEval_SetSwitchInterval(LONG_MAX);

    //See answer for http://stackoverflow.com/questions/15470367/pyeval-initthreads-in-python-3-how-when-to-call-it-the-saga-continues-ad-naus
    PyEval_InitThreads();

    ///Do as per http://wiki.blender.org/index.php/Dev:2.4/Source/Python/API/Threads
    ///All calls to the Python API should call PythonGILLocker beforehand.
    //_imp->mainThreadState = PyGILState_GetThisThreadState();
    //PyEval_ReleaseThread(_imp->mainThreadState);

    std::string err;
#if defined(NATRON_CONFIG_SNAPSHOT) || defined(DEBUG)
    /// print info about python lib
    {
        printf( "PATH is %s\n", Py_GETENV("PATH") );
        printf( "PYTHONPATH is %s\n", Py_GETENV("PYTHONPATH") );
        printf( "PYTHONHOME is %s\n", Py_GETENV("PYTHONHOME") );
        printf( "Py_DebugFlag is %d\n", Py_DebugFlag );
        printf( "Py_VerboseFlag is %d\n", Py_VerboseFlag );
        printf( "Py_InteractiveFlag is %d\n", Py_InteractiveFlag );
        printf( "Py_InspectFlag is %d\n", Py_InspectFlag );
        printf( "Py_OptimizeFlag is %d\n", Py_OptimizeFlag );
        printf( "Py_NoSiteFlag is %d\n", Py_NoSiteFlag );
        printf( "Py_BytesWarningFlag is %d\n", Py_BytesWarningFlag );
        printf( "Py_UseClassExceptionsFlag is %d\n", Py_UseClassExceptionsFlag );
        printf( "Py_FrozenFlag is %d\n", Py_FrozenFlag );
        printf( "Py_TabcheckFlag is %d\n", Py_TabcheckFlag );
        printf( "Py_UnicodeFlag is %d\n", Py_UnicodeFlag );
        printf( "Py_IgnoreEnvironmentFlag is %d\n", Py_IgnoreEnvironmentFlag );
        printf( "Py_DivisionWarningFlag is %d\n", Py_DivisionWarningFlag );
        printf( "Py_DontWriteBytecodeFlag is %d\n", Py_DontWriteBytecodeFlag );
        printf( "Py_NoUserSiteDirectory is %d\n", Py_NoUserSiteDirectory );
        printf( "Py_GetProgramName is %s\n", Py_GetProgramName() );
        printf( "Py_GetPrefix is %s\n", Py_GetPrefix() );
        printf( "Py_GetExecPrefix is %s\n", Py_GetPrefix() );
        printf( "Py_GetProgramFullPath is %s\n", Py_GetProgramFullPath() );
        printf( "Py_GetPath is %s\n", Py_GetPath() );
        printf( "Py_GetPythonHome is %s\n", Py_GetPythonHome() );
        bool ok = NATRON_PYTHON_NAMESPACE::interpretPythonScript("from distutils.sysconfig import get_python_lib; print('Python library is in ' + get_python_lib())", &err, 0);
        assert(ok);
        Q_UNUSED(ok);
    }
#endif

    // Import NatronEngine
    std::string modulename = NATRON_ENGINE_PYTHON_MODULE_NAME;
    bool ok = NATRON_PYTHON_NAMESPACE::interpretPythonScript("import sys\nfrom math import *\nimport " + modulename, &err, 0);
    if (!ok) {
        throw std::runtime_error( tr("Error while loading python module %1: %2").arg( QString::fromUtf8( modulename.c_str() ) ).arg( QString::fromUtf8( err.c_str() ) ).toStdString() );
    }

    // Create NatronEngine.natron wrapper
    ok = NATRON_PYTHON_NAMESPACE::interpretPythonScript(modulename + ".natron = " + modulename + ".PyCoreApplication()\n", &err, 0);
    assert(ok);
    if (!ok) {
        throw std::runtime_error( tr("Error while loading python module %1: %2").arg( QString::fromUtf8( modulename.c_str() ) ).arg( QString::fromUtf8( err.c_str() ) ).toStdString() );
    }

    if ( !isBackground() ) {
        // Import NatronGui
        modulename = NATRON_GUI_PYTHON_MODULE_NAME;
        ok = NATRON_PYTHON_NAMESPACE::interpretPythonScript("import sys\nimport " + modulename, &err, 0);
        assert(ok);
        if (!ok) {
            throw std::runtime_error( tr("Error while loading python module %1: %2").arg( QString::fromUtf8( modulename.c_str() ) ).arg( QString::fromUtf8( err.c_str() ) ).toStdString() );
        }

        // Create NatronGui.natron wrapper
        ok = NATRON_PYTHON_NAMESPACE::interpretPythonScript(modulename + ".natron = " +
                                                            modulename + ".PyGuiApplication()\n", &err, 0);
        assert(ok);
        if (!ok) {
            throw std::runtime_error( tr("Error while loading python module %1: %2").arg( QString::fromUtf8( modulename.c_str() ) ).arg( QString::fromUtf8( err.c_str() ) ).toStdString() );
        }
    }

    // redirect stdout/stderr
    std::string script(
                       "class StreamCatcher:\n"
                       "   def __init__(self):\n"
                       "       self.value = ''\n"
                       "   def write(self,txt):\n"
                       "       self.value += txt\n"
                       "   def clear(self):\n"
                       "       self.value = ''\n"
                       "catchOut = StreamCatcher()\n"
                       "catchErr = StreamCatcher()\n"
                       "sys.stdout = catchOut\n"
                       "sys.stderr = catchErr\n");
    ok = NATRON_PYTHON_NAMESPACE::interpretPythonScript(script, &err, 0);
    assert(ok);
    if (!ok) {
        throw std::runtime_error( tr("Error while loading StreamCatcher: %1").arg( QString::fromUtf8( err.c_str() ) ).toStdString() );
    }
} // AppManager::initPython

void
AppManager::tearDownPython()
{
#ifdef NATRON_RUN_WITHOUT_PYTHON

    return;
#endif
    ///See http://wiki.blender.org/index.php/Dev:2.4/Source/Python/API/Threads
    //PyGILState_Ensure();

    Py_DECREF(_imp->mainModule);
    Py_Finalize();
}

PyObject*
AppManager::getMainModule()
{
    return _imp->mainModule;
}

///The symbol has been generated by Shiboken in  Engine/NatronEngine/natronengine_module_wrapper.cpp
extern "C"
{
#if PY_MAJOR_VERSION >= 3
// Python 3
PyObject* PyInit_NatronEngine();
#else
void initNatronEngine();
#endif
}

void
AppManager::initBuiltinPythonModules()
{
#if PY_MAJOR_VERSION >= 3
    // Python 3
    int ret = PyImport_AppendInittab(NATRON_ENGINE_PYTHON_MODULE_NAME, &PyInit_NatronEngine);
#else
    int ret = PyImport_AppendInittab(NATRON_ENGINE_PYTHON_MODULE_NAME, &initNatronEngine);
#endif
    if (ret == -1) {
        throw std::runtime_error("Failed to initialize built-in Python module.");
    }
}

void
AppManager::toggleAutoHideGraphInputs()
{
    AppInstanceVec copy;
    {
        QMutexLocker k(&_imp->_appInstancesMutex);
        copy = _imp->_appInstances;
    }

    for (AppInstanceVec::iterator it = copy.begin(); it != copy.end(); ++it) {
        (*it)->toggleAutoHideGraphInputs();
    }
}

void
AppManager::launchPythonInterpreter()
{
    std::string err;
    std::string s = "app = app1\n";
    bool ok = NATRON_PYTHON_NAMESPACE::interpretPythonScript(s, &err, 0);

    assert(ok);
    if (!ok) {
        throw std::runtime_error("AppInstance::launchPythonInterpreter(): interpretPythonScript(" + s + " failed!");
    }

    // PythonGILLocker pgl;
#if PY_MAJOR_VERSION >= 3
    // Python 3
    Py_Main(1, &_imp->commandLineArgsWide[0]);
#else
    Py_Main(1, &_imp->commandLineArgsUtf8[0]);
#endif

}

int
AppManager::isProjectAlreadyOpened(const std::string& projectFilePath) const
{
    QMutexLocker k(&_imp->_appInstancesMutex);

    for (AppInstanceVec::iterator it = _imp->_appInstances.begin(); it != _imp->_appInstances.end(); ++it) {
        ProjectPtr proj = (*it)->getProject();
        if (proj) {
            QString path = proj->getProjectPath();
            QString name = proj->getProjectFilename();
            std::string existingProject = path.toStdString() + name.toStdString();
            if (existingProject == projectFilePath) {
                return (*it)->getAppID();
            }
        }
    }

    return -1;
}

void
AppManager::onCrashReporterNoLongerResponding()
{
#ifdef NATRON_USE_BREAKPAD
    //Crash reporter seems to no longer exist, quit
    QString error = tr("%1 has detected that the crash reporter process is no longer responding. "
                       "This most likely indicates that it was killed or that the "
                       "communication between the 2 processes is failing.")
                    .arg( QString::fromUtf8(NATRON_APPLICATION_NAME) );
    std::cerr << error.toStdString() << std::endl;
    writeToErrorLog_mt_safe(tr("Crash-Reporter"), QDateTime::currentDateTime(), error );
#endif
}

void
AppManager::setOnProjectLoadedCallback(const std::string& pythonFunc)
{
    _imp->_settings->setOnProjectLoadedCB(pythonFunc);
}

void
AppManager::setOnProjectCreatedCallback(const std::string& pythonFunc)
{
    _imp->_settings->setOnProjectCreatedCB(pythonFunc);
}

OFX::Host::ImageEffect::Descriptor*
AppManager::getPluginContextAndDescribe(OFX::Host::ImageEffect::ImageEffectPlugin* plugin,
                                        ContextEnum* ctx)
{
    return _imp->ofxHost->getPluginContextAndDescribe(plugin, ctx);
}

std::list<std::string>
AppManager::getNatronPath()
{
    std::list<std::string> ret;
    QStringList p = appPTR->getAllNonOFXPluginsPaths();

    for (QStringList::iterator it = p.begin(); it != p.end(); ++it) {
        ret.push_back( it->toStdString() );
    }

    return ret;
}

void
AppManager::appendToNatronPath(const std::string& path)
{
    appPTR->getCurrentSettings()->appendPythonGroupsPath(path);
}

#ifdef __NATRON_WIN32__
void
AppManager::registerUNCPath(const QString& path,
                            const QChar& driveLetter)
{
    assert( QThread::currentThread() == qApp->thread() );
    _imp->uncPathMapping[driveLetter] = path;
}

QString
AppManager::mapUNCPathToPathWithDriveLetter(const QString& uncPath) const
{
    assert( QThread::currentThread() == qApp->thread() );
    if ( uncPath.isEmpty() ) {
        return uncPath;
    }
    for (std::map<QChar, QString>::const_iterator it = _imp->uncPathMapping.begin(); it != _imp->uncPathMapping.end(); ++it) {
        int index = uncPath.indexOf(it->second);
        if (index == 0) {
            //We found the UNC mapping at the start of the path, replace it with a drive letter
            QString ret = uncPath;
            ret.remove( 0, it->second.size() );
            QString drive;
            drive.append(it->first);
            drive.append( QLatin1Char(':') );
            if ( !ret.isEmpty() && !ret.startsWith( QLatin1Char('/') ) ) {
                drive.append( QLatin1Char('/') );
            }
            ret.prepend(drive);

            return ret;
        }
    }

    return uncPath;
}

#endif

const IOPluginsMap&
AppManager::getFileFormatsForReadingAndReader() const
{
    return _imp->readerPlugins;
}

const IOPluginsMap&
AppManager::getFileFormatsForWritingAndWriter() const
{
    return _imp->writerPlugins;
}

void
AppManager::getSupportedReaderFileFormats(std::vector<std::string>* formats) const
{
    const IOPluginsMap& readersForFormat = getFileFormatsForReadingAndReader();

    formats->resize( readersForFormat.size() );
    int i = 0;
    for (IOPluginsMap::const_iterator it = readersForFormat.begin(); it != readersForFormat.end(); ++it, ++i) {
        (*formats)[i] = it->first;
    }
}

void
AppManager::getSupportedWriterFileFormats(std::vector<std::string>* formats) const
{
    const IOPluginsMap& writersForFormat = getFileFormatsForWritingAndWriter();

    formats->resize( writersForFormat.size() );
    int i = 0;
    for (IOPluginsMap::const_iterator it = writersForFormat.begin(); it != writersForFormat.end(); ++it, ++i) {
        (*formats)[i] = it->first;
    }
}

void
AppManager::getReadersForFormat(const std::string& format,
                                IOPluginSetForFormat* decoders) const
{
    // This will perform a case insensitive find
    IOPluginsMap::const_iterator found = _imp->readerPlugins.find(format);

    if ( found == _imp->readerPlugins.end() ) {
        return;
    }
    *decoders = found->second;
}

void
AppManager::getWritersForFormat(const std::string& format,
                                IOPluginSetForFormat* encoders) const
{
    // This will perform a case insensitive find
    IOPluginsMap::const_iterator found = _imp->writerPlugins.find(format);

    if ( found == _imp->writerPlugins.end() ) {
        return;
    }
    *encoders = found->second;
}

std::string
AppManager::getReaderPluginIDForFileType(const std::string & extension) const
{
    // This will perform a case insensitive find
    IOPluginsMap::const_iterator found = _imp->readerPlugins.find(extension);

    if ( found == _imp->readerPlugins.end() ) {
        return std::string();
    }
    // Return the "best" plug-in (i.e: higher score)

    return found->second.empty() ? std::string() : found->second.rbegin()->pluginID;
}

std::string
AppManager::getWriterPluginIDForFileType(const std::string & extension) const
{
    // This will perform a case insensitive find
    IOPluginsMap::const_iterator found = _imp->writerPlugins.find(extension);

    if ( found == _imp->writerPlugins.end() ) {
        return std::string();
    }
    // Return the "best" plug-in (i.e: higher score)

    return found->second.empty() ? std::string() : found->second.rbegin()->pluginID;
}


AppTLS*
AppManager::getAppTLS() const
{
    return &_imp->globalTLS;
}


QString
AppManager::getBoostVersion() const
{
    return QString::fromUtf8(BOOST_LIB_VERSION);
}

QString
AppManager::getQtVersion() const
{
    return QString::fromUtf8(QT_VERSION_STR) + QString::fromUtf8(" / ") + QString::fromUtf8( qVersion() );
}

QString
AppManager::getCairoVersion() const
{
#ifdef ROTO_SHAPE_RENDER_ENABLE_CAIRO
    return RotoShapeRenderCairo::getCairoVersion();
#else
    return QString();
#endif
}


QString
AppManager::getHoedownVersion() const
{
    int major, minor, revision;
    hoedown_version(&major, &minor, &revision);
    return QString::fromUtf8(HOEDOWN_VERSION) + QString::fromUtf8(" / ") + QString::fromUtf8("%1.%2.%3").arg(major).arg(minor).arg(revision);
}


QString
AppManager::getCeresVersion() const
{
    return QString::fromUtf8(CERES_VERSION_STRING);
}


QString
AppManager::getOpenMVGVersion() const
{
    return QString::fromUtf8(OPENMVG_VERSION_STRING);
}


QString
AppManager::getPySideVersion() const
{
    return QString::fromUtf8(SHIBOKEN_VERSION);
}

const NATRON_NAMESPACE::OfxHost*
AppManager::getOFXHost() const
{
    return _imp->ofxHost.get();
}

GPUContextPool*
AppManager::getGPUContextPool() const
{
    return _imp->renderingContextPool.get();
}

void
AppManager::refreshOpenGLRenderingFlagOnAllInstances()
{
    for (std::size_t i = 0; i < _imp->_appInstances.size(); ++i) {
        _imp->_appInstances[i]->getProject()->refreshOpenGLRenderingFlagOnNodes();
    }
}

void
Dialogs::errorDialog(const std::string & title,
                     const std::string & message,
                     bool useHtml)
{
    appPTR->hideSplashScreen();
    AppInstancePtr topLvlInstance = appPTR->getTopLevelInstance();
    if ( topLvlInstance && !appPTR->isBackground() ) {
        topLvlInstance->errorDialog(title, message, useHtml);
    } else {
        std::cerr << "ERROR: " << title << ": " <<  message << std::endl;
    }
}

void
Dialogs::errorDialog(const std::string & title,
                     const std::string & message,
                     bool* stopAsking,
                     bool useHtml)
{
    appPTR->hideSplashScreen();
    AppInstancePtr topLvlInstance = appPTR->getTopLevelInstance();
    if ( topLvlInstance && !appPTR->isBackground() ) {
        topLvlInstance->errorDialog(title, message, stopAsking, useHtml);
    } else {
        std::cerr << "ERROR: " << title << ": " <<  message << std::endl;
    }
}

void
Dialogs::warningDialog(const std::string & title,
                       const std::string & message,
                       bool useHtml)
{
    appPTR->hideSplashScreen();
    AppInstancePtr topLvlInstance = appPTR->getTopLevelInstance();
    if ( topLvlInstance && !appPTR->isBackground() ) {
        topLvlInstance->warningDialog(title, message, useHtml);
    } else {
        std::cerr << "WARNING: " << title << ": " << message << std::endl;
    }
}

void
Dialogs::warningDialog(const std::string & title,
                       const std::string & message,
                       bool* stopAsking,
                       bool useHtml)
{
    appPTR->hideSplashScreen();
    AppInstancePtr topLvlInstance = appPTR->getTopLevelInstance();
    if ( topLvlInstance && !appPTR->isBackground() ) {
        topLvlInstance->warningDialog(title, message, stopAsking, useHtml);
    } else {
        std::cerr << "WARNING: " << title << ":" << message << std::endl;
    }
}

void
Dialogs::informationDialog(const std::string & title,
                           const std::string & message,
                           bool useHtml)
{
    appPTR->hideSplashScreen();
    AppInstancePtr topLvlInstance = appPTR->getTopLevelInstance();
    if ( topLvlInstance && !appPTR->isBackground() ) {
        topLvlInstance->informationDialog(title, message, useHtml);
    } else {
        std::cout << "INFO: " << title << ":" << message << std::endl;
    }
}

void
Dialogs::informationDialog(const std::string & title,
                           const std::string & message,
                           bool* stopAsking,
                           bool useHtml)
{
    appPTR->hideSplashScreen();
    AppInstancePtr topLvlInstance = appPTR->getTopLevelInstance();
    if ( topLvlInstance && !appPTR->isBackground() ) {
        topLvlInstance->informationDialog(title, message, stopAsking, useHtml);
    } else {
        std::cout << "INFO: " << title << ":" << message << std::endl;
    }
}

StandardButtonEnum
Dialogs::questionDialog(const std::string & title,
                        const std::string & message,
                        bool useHtml,
                        StandardButtons buttons,
                        StandardButtonEnum defaultButton)
{
    appPTR->hideSplashScreen();
    AppInstancePtr topLvlInstance = appPTR->getTopLevelInstance();
    if ( topLvlInstance && !appPTR->isBackground() ) {
        return topLvlInstance->questionDialog(title, message, useHtml, buttons, defaultButton);
    } else {
        std::cout << "QUESTION ASKED: " << title << ":" << message << std::endl;
        std::cout << NATRON_APPLICATION_NAME " answered yes." << std::endl;

        return eStandardButtonYes;
    }
}

StandardButtonEnum
Dialogs::questionDialog(const std::string & title,
                        const std::string & message,
                        bool useHtml,
                        StandardButtons buttons,
                        StandardButtonEnum defaultButton,
                        bool* stopAsking)
{
    appPTR->hideSplashScreen();
    AppInstancePtr topLvlInstance = appPTR->getTopLevelInstance();
    if ( topLvlInstance && !appPTR->isBackground() ) {
        return topLvlInstance->questionDialog(title, message, useHtml, buttons, defaultButton, stopAsking);
    } else {
        std::cout << "QUESTION ASKED: " << title << ":" << message << std::endl;
        std::cout << NATRON_APPLICATION_NAME " answered yes." << std::endl;

        return eStandardButtonYes;
    }
}

#if 0 // dead code
std::size_t
NATRON_PYTHON_NAMESPACE::findNewLineStartAfterImports(std::string& script)
{
    ///Find position of the last import
    size_t foundImport = script.find("import ");

    if (foundImport != std::string::npos) {
        for (;; ) {
            size_t found = script.find("import ", foundImport + 1);
            if (found == std::string::npos) {
                break;
            } else {
                foundImport = found;
            }
        }
    }

    if (foundImport == std::string::npos) {
        return 0;
    }

    ///find the next end line aftr the import
    size_t endLine = script.find('\n', foundImport + 1);


    if (endLine == std::string::npos) {
        //no end-line, add one
        script.append("\n");

        return script.size();
    } else {
        return endLine + 1;
    }
}

#endif

PyObject*
NATRON_PYTHON_NAMESPACE::getMainModule()
{
    return appPTR->getMainModule();
}

#if 0 // dead code
std::size_t
NATRON_PYTHON_NAMESPACE::ensureScriptHasModuleImport(const std::string& moduleName,
                                                     std::string& script)
{
    /// import module
    script = "from " + moduleName + " import * \n" + script;

    return NATRON_PYTHON_NAMESPACE::findNewLineStartAfterImports(script);
}

#endif

bool
NATRON_PYTHON_NAMESPACE::interpretPythonScript(const std::string& script,
                                               std::string* error,
                                               std::string* output)
{
#ifdef NATRON_RUN_WITHOUT_PYTHON

    return true;
#endif
    PythonGILLocker pgl;
    PyObject* mainModule = NATRON_PYTHON_NAMESPACE::getMainModule();
    PyObject* dict = PyModule_GetDict(mainModule);

    ///This is faster than PyRun_SimpleString since is doesn't call PyImport_AddModule("__main__")
    PyObject* v = PyRun_String(script.c_str(), Py_file_input, dict, 0);
    if (v) {
        Py_DECREF(v);
    }

    PyObject *errCatcher = 0;
    PyObject *outCatcher = 0;

    if ( PyObject_HasAttrString(mainModule, "catchErr") ) {
        errCatcher = PyObject_GetAttrString(mainModule, "catchErr"); //get our catchOutErr created above, new ref
    }

    if ( PyObject_HasAttrString(mainModule, "catchOut") ) {
        outCatcher = PyObject_GetAttrString(mainModule, "catchOut"); //get our catchOutErr created above, new ref
    }

    PyErr_Print(); //make python print any errors

    PyObject *errorObj = 0;
    std::string tmpError;
    if (errCatcher) {
        errorObj = PyObject_GetAttrString(errCatcher, "value"); //get the  stderr from our catchErr object, new ref
        assert(errorObj);
        tmpError = PyStringToStdString(errorObj);
        if (error) {
            *error = tmpError;
        }
        PyObject* unicode = PyUnicode_FromString("");
        PyObject_SetAttrString(errCatcher, "value", unicode);
        Py_DECREF(errorObj);
        Py_DECREF(errCatcher);
    }
    PyObject *outObj = 0;
    if (outCatcher) {
        outObj = PyObject_GetAttrString(outCatcher, "value"); //get the stdout from our catchOut object, new ref
        assert(outObj);
        if (output) {
            *output = PyStringToStdString(outObj);
        }
        PyObject* unicode = PyUnicode_FromString("");
        PyObject_SetAttrString(outCatcher, "value", unicode);
        Py_DECREF(outObj);
        Py_DECREF(outCatcher);
    }

    if ( !tmpError.empty() ) {
        if (error) {
            *error = "While executing script:\n" + script + "Python error:\n" + *error;
        }

        return false;
    }

    return true;

} // NATRON_PYTHON_NAMESPACE::interpretPythonScript

#if 0 // dead code
void
NATRON_PYTHON_NAMESPACE::compilePyScript(const std::string& script,
                                         PyObject** code)
{
    ///Must be locked
    assert( PyThreadState_Get() );

    *code = (PyObject*)Py_CompileString(script.c_str(), "<string>", Py_file_input);
    if (PyErr_Occurred() || !*code) {
#ifdef DEBUG
        PyErr_Print();
#endif
        throw std::runtime_error("failed to compile the script");
    }
}

#endif

static std::string
makeNameScriptFriendlyInternal(const std::string& str,
                               bool allowDots)
{
    if (str == "from") {
        return "pFrom";
    } else if (str == "lambda") {
        return "pLambda";
    }
    ///Remove any non alpha-numeric characters from the baseName
    std::locale loc;
    std::string cpy;
    for (std::size_t i = 0; i < str.size(); ++i) {
        ///Ignore starting digits
        if ( cpy.empty() && std::isdigit(str[i], loc) ) {
            cpy.push_back('p');
            cpy.push_back(str[i]);
            continue;
        }

        ///Spaces becomes underscores
        if ( std::isspace(str[i], loc) ) {
            cpy.push_back('_');
        }
        ///Non alpha-numeric characters are not allowed in python
        else if ( (str[i] == '_') || std::isalnum(str[i], loc) || ( allowDots && (str[i] == '.') ) ) {
            cpy.push_back(str[i]);
        }
    }

    return cpy;
}

std::string
NATRON_PYTHON_NAMESPACE::makeNameScriptFriendlyWithDots(const std::string& str)
{
    return makeNameScriptFriendlyInternal(str, true);
}

std::string
NATRON_PYTHON_NAMESPACE::makeNameScriptFriendly(const std::string& str)
{
    return makeNameScriptFriendlyInternal(str, false);
}

PythonGILLocker::PythonGILLocker()
//    : state(PyGILState_UNLOCKED)
{
    if (appPTR) {
        appPTR->takeNatronGIL();
    }
//    ///Take the GIL for this thread
//    state = PyGILState_Ensure();
//    assert(PyThreadState_Get());
//#if !defined(NDEBUG) && PY_VERSION_HEX >= 0x030400F0
//    assert(PyGILState_Check()); // Not available prior to Python 3.4
//#endif
}

PythonGILLocker::~PythonGILLocker()
{
    if (appPTR) {
        appPTR->releaseNatronGIL();
    }

//#if !defined(NDEBUG) && PY_VERSION_HEX >= 0x030400F0
//    assert(PyGILState_Check());  // Not available prior to Python 3.4
//#endif
//
//    ///Release the GIL, no thread will own it afterwards.
//    PyGILState_Release(state);
}

static bool
getGroupInfosInternal(const std::string& pythonModule,
                      std::string* pluginID,
                      std::string* pluginLabel,
                      std::string* iconFilePath,
                      std::string* grouping,
                      std::string* description,
                      std::string* pythonScriptDirPath,
                      bool* isToolset,
                      unsigned int* version)
{
#ifdef NATRON_RUN_WITHOUT_PYTHON

    return false;
#endif
    PythonGILLocker pgl;
    static const QString script = QString::fromUtf8("import sys\n"
                                                    "import os.path\n"
                                                    "import %1\n"
                                                    "ret = True\n"
                                                    "if not hasattr(%1,\"createInstance\") or not hasattr(%1.createInstance,\"__call__\"):\n"
                                                    "    ret = False\n"
                                                    "if not hasattr(%1,\"getLabel\") or not hasattr(%1.getLabel,\"__call__\"):\n"
                                                    "    ret = False\n"
                                                    "templateLabel=\"\"\n"
                                                    "if ret == True:\n"
                                                    "    templateLabel = %1.getLabel()\n"
                                                    "pluginID = templateLabel\n"
                                                    "version = 1\n"
                                                    "isToolset = False\n"
                                                    "pythonScriptAbsFilePath = os.path.dirname(%1.__file__)\n"
                                                    "if hasattr(%1,\"getVersion\") and hasattr(%1.getVersion,\"__call__\"):\n"
                                                    "    version = %1.getVersion()\n"
                                                    "if hasattr(%1,\"getIsToolset\") and hasattr(%1.getIsToolset,\"__call__\"):\n"
                                                    "    isToolset = %1.getIsToolset()\n"
                                                    "description=\"\"\n"
                                                    "if hasattr(%1,\"getPluginDescription\") and hasattr(%1.getPluginDescription,\"__call__\"):\n"
                                                    "    description = %1.getPluginDescription()\n"
                                                    "elif hasattr(%1,\"getDescription\") and hasattr(%1.getDescription,\"__call__\"):\n" // Check old function name for compat
                                                    "    description = %1.getDescription()\n"
                                                    "if hasattr(%1,\"getPluginID\") and hasattr(%1.getPluginID,\"__call__\"):\n"
                                                    "    pluginID = %1.getPluginID()\n"
                                                    "if ret == True and hasattr(%1,\"getIconPath\") and hasattr(%1.getIconPath,\"__call__\"):\n"
                                                    "    global templateIcon\n"
                                                    "    templateIcon = %1.getIconPath()\n"
                                                    "if ret == True and hasattr(%1,\"getGrouping\") and hasattr(%1.getGrouping,\"__call__\"):\n"
                                                    "    global templateGrouping\n"
                                                    "    templateGrouping =  %1.getGrouping()\n");
    std::string toRun = script.arg( QString::fromUtf8( pythonModule.c_str() ) ).toStdString();
    std::string err;
    if ( !NATRON_PYTHON_NAMESPACE::interpretPythonScript(toRun, &err, 0) ) {
        QString logStr = QCoreApplication::translate("AppManager", "Was not recognized as a PyPlug: %1").arg( QString::fromUtf8( err.c_str() ) );
        appPTR->writeToErrorLog_mt_safe(QString::fromUtf8(pythonModule.c_str()), QDateTime::currentDateTime(), logStr);

        return false;
    }

    PyObject* mainModule = NATRON_PYTHON_NAMESPACE::getMainModule();
    PyObject* retObj = PyObject_GetAttrString(mainModule, "ret"); //new ref
    assert(retObj);
    if (PyObject_IsTrue(retObj) == 0) {
        Py_XDECREF(retObj);

        return false;
    }
    Py_XDECREF(retObj);

    std::string deleteScript("del ret\n"
                             "del templateLabel\n");

    PyObject* pythonScriptFilePathObj = 0;
    pythonScriptFilePathObj = PyObject_GetAttrString(mainModule, "pythonScriptAbsFilePath"); //new ref

    PyObject* labelObj = 0;
    labelObj = PyObject_GetAttrString(mainModule, "templateLabel"); //new ref

    PyObject* idObj = 0;
    idObj = PyObject_GetAttrString(mainModule, "pluginID"); //new ref

    PyObject* iconObj = 0;
    if ( PyObject_HasAttrString(mainModule, "templateIcon") ) {
        iconObj = PyObject_GetAttrString(mainModule, "templateIcon"); //new ref
    }
    PyObject* iconGrouping = 0;
    if ( PyObject_HasAttrString(mainModule, "templateGrouping") ) {
        iconGrouping = PyObject_GetAttrString(mainModule, "templateGrouping"); //new ref
    }

    PyObject* versionObj = 0;
    if ( PyObject_HasAttrString(mainModule, "version") ) {
        versionObj = PyObject_GetAttrString(mainModule, "version"); //new ref
    }

    PyObject* isToolsetObj = 0;
    if ( PyObject_HasAttrString(mainModule, "isToolset") ) {
        isToolsetObj = PyObject_GetAttrString(mainModule, "isToolset"); //new ref
    }

    PyObject* pluginDescriptionObj = 0;
    if ( PyObject_HasAttrString(mainModule, "description") ) {
        pluginDescriptionObj = PyObject_GetAttrString(mainModule, "description"); //new ref
    }

    assert(labelObj && pythonScriptFilePathObj);


    QString modulePath;
    {
        std::string modulePYCAbsoluteFilePath = NATRON_PYTHON_NAMESPACE::PyStringToStdString(pythonScriptFilePathObj);
#ifdef IS_PYTHON_2
        modulePath = QString::fromUtf8(modulePYCAbsoluteFilePath.c_str());
#else
        Py_XDECREF(pythonScriptFilePathObj);

        QString q_modulePYCAbsoluteFilePath = QString::fromUtf8(modulePYCAbsoluteFilePath.c_str());
        QtCompat::removeFileExtension(q_modulePYCAbsoluteFilePath);
        int foundLastSlash = q_modulePYCAbsoluteFilePath.lastIndexOf( QChar::fromLatin1('/') );
        if (foundLastSlash != -1) {
            modulePath = q_modulePYCAbsoluteFilePath.mid(0, foundLastSlash);
        }
#endif
    }

    *pythonScriptDirPath = modulePath.toStdString();

    *pluginLabel = NATRON_PYTHON_NAMESPACE::PyStringToStdString(labelObj);
    Py_XDECREF(labelObj);
    
    if (idObj) {
        *pluginID = NATRON_PYTHON_NAMESPACE::PyStringToStdString(idObj);
        deleteScript.append("del pluginID\n");
        Py_XDECREF(idObj);
    }

    if (iconObj) {
        *iconFilePath = NATRON_PYTHON_NAMESPACE::PyStringToStdString(iconObj);
        QFileInfo iconInfo(modulePath + QString::fromUtf8( iconFilePath->c_str() ) );
        *iconFilePath =  iconInfo.canonicalFilePath().toStdString();

        deleteScript.append("del templateIcon\n");
        Py_XDECREF(iconObj);
    }
    if (iconGrouping) {
        *grouping = NATRON_PYTHON_NAMESPACE::PyStringToStdString(iconGrouping);
        deleteScript.append("del templateGrouping\n");
        Py_XDECREF(iconGrouping);
    }

    if (versionObj) {
        *version = (unsigned int)PyLong_AsLong(versionObj);
        deleteScript.append("del version\n");
        Py_XDECREF(versionObj);
    }

    if ( isToolsetObj && PyBool_Check(isToolsetObj) ) {
        *isToolset = (isToolsetObj == Py_True) ? true : false;
        deleteScript.append("del isToolset\n");
        Py_XDECREF(isToolsetObj);
    }


    if (pluginDescriptionObj) {
        *description = NATRON_PYTHON_NAMESPACE::PyStringToStdString(pluginDescriptionObj);
        deleteScript.append("del description\n");
        Py_XDECREF(pluginDescriptionObj);
    }

    if ( grouping->empty() ) {
        *grouping = PLUGIN_GROUP_OTHER;
    }


    bool ok = NATRON_PYTHON_NAMESPACE::interpretPythonScript(deleteScript, &err, NULL);
    assert(ok);
    if (!ok) {
        throw std::runtime_error("getGroupInfos(): interpretPythonScript(" + deleteScript + " failed!");
    }

    return true;
} // getGroupInfosInternal


bool
NATRON_PYTHON_NAMESPACE::getGroupInfos(const std::string& pythonModule,
                                       std::string* pluginID,
                                       std::string* pluginLabel,
                                       std::string* iconFilePath,
                                       std::string* grouping,
                                       std::string* description,
                                       std::string* pythonScriptDirPath,
                                       bool* isToolset,
                                       unsigned int* version)
{
#ifdef NATRON_RUN_WITHOUT_PYTHON

    return false;
#endif
    return getGroupInfosInternal(pythonModule, pluginID, pluginLabel, iconFilePath, grouping, description, pythonScriptDirPath, isToolset, version);
}

void
NATRON_PYTHON_NAMESPACE::getFunctionArguments(const std::string& pyFunc,
                                              std::string* error,
                                              std::vector<std::string>* args)
{
#ifdef NATRON_RUN_WITHOUT_PYTHON

    return;
#endif
    std::stringstream ss;
    ss << "import inspect\n";
    ss << "args_spec = inspect.getargspec(" << pyFunc << ")\n";
    std::string script = ss.str();
    std::string output;
    bool ok = NATRON_PYTHON_NAMESPACE::interpretPythonScript(script, error, &output);
    if (!ok) {
        throw std::runtime_error("NATRON_PYTHON_NAMESPACE::getFunctionArguments(): interpretPythonScript(" + script + " failed!");
    }
    PyObject* mainModule = NATRON_PYTHON_NAMESPACE::getMainModule();
    PyObject* args_specObj = 0;
    if ( PyObject_HasAttrString(mainModule, "args_spec") ) {
        args_specObj = PyObject_GetAttrString(mainModule, "args_spec");
    }
    assert(args_specObj);
    PyObject* argListObj = 0;

    if (args_specObj) {
        argListObj = PyTuple_GetItem(args_specObj, 0);
        assert(argListObj);
        if (argListObj) {
            // size = PyObject_Size(argListObj)
            assert( PyList_Check(argListObj) );
            Py_ssize_t size = PyList_Size(argListObj);
            for (Py_ssize_t i = 0; i < size; ++i) {
                PyObject* itemObj = PyList_GetItem(argListObj, i);
                assert(itemObj);
                if (itemObj) {
                    std::string itemName = PyStringToStdString(itemObj);
                    assert( !itemName.empty() );
                    if ( !itemName.empty() ) {
                        args->push_back(itemName);
                    }
                }
            }
            if ( (PyTuple_GetItem(args_specObj, 1) != Py_None) || (PyTuple_GetItem(args_specObj, 2) != Py_None) ) {
                error->append("Function contains variadic arguments which is unsupported.");

                return;
            }
        }
    }
}

/**
 * @brief Given a fullyQualifiedName, e.g: app1.Group1.Blur1
 * this function returns the PyObject attribute of Blur1 if it is defined, or Group1 otherwise
 * If app1 or Group1 does not exist at this point, this is a failure.
 **/
PyObject*
NATRON_PYTHON_NAMESPACE::getAttrRecursive(const std::string& fullyQualifiedName,
                                          PyObject* parentObj,
                                          bool* isDefined)
{
#ifdef NATRON_RUN_WITHOUT_PYTHON

    return 0;
#endif
    std::size_t foundDot = fullyQualifiedName.find(".");
    std::string attrName = foundDot == std::string::npos ? fullyQualifiedName : fullyQualifiedName.substr(0, foundDot);
    PyObject* obj = 0;
    if ( PyObject_HasAttrString( parentObj, attrName.c_str() ) ) {
        obj = PyObject_GetAttrString( parentObj, attrName.c_str() );
    }

    ///We either found the parent object or we are on the last object in which case we return the parent
    if (!obj) {
        //assert(fullyQualifiedName.find(".") == std::string::npos);
        *isDefined = false;

        return parentObj;
    } else {
        std::string recurseName;
        if (foundDot != std::string::npos) {
            recurseName = fullyQualifiedName;
            recurseName.erase(0, foundDot + 1);
        }
        if ( !recurseName.empty() ) {
            return NATRON_PYTHON_NAMESPACE::getAttrRecursive(recurseName, obj, isDefined);
        } else {
            *isDefined = true;

            return obj;
        }
    }
}

NATRON_NAMESPACE_EXIT;

NATRON_NAMESPACE_USING;
#include "moc_AppManager.cpp"
