/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2013-2017 INRIA and Alexandre Gauthier-Foichat
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

#ifndef ENGINE_KNOBPRIVATE_H
#define ENGINE_KNOBPRIVATE_H

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

#include "Knob.h"

#include <algorithm> // min, max
#include <cassert>
#include <stdexcept>

#include <QtCore/QDataStream>
#include <QtCore/QDateTime>
#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <QtCore/QDebug>

#if !defined(SBK_RUN) && !defined(Q_MOC_RUN)
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_OFF
#include <boost/algorithm/string/predicate.hpp>
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_ON
#endif

#include "Global/GlobalDefines.h"

#include "Engine/AppInstance.h"
#include "Engine/AppManager.h"
#include "Engine/Curve.h"
#include "Engine/Cache.h"
#include "Engine/CacheEntryBase.h"
#include "Engine/CacheEntryKeyBase.h"
#include "Engine/DockablePanelI.h"
#include "Engine/Hash64.h"
#include "Engine/KnobFile.h"
#include "Engine/KnobTypes.h"
#include "Engine/LibraryBinary.h"
#include "Engine/Node.h"
#include "Engine/Project.h"
#include "Engine/StringAnimationManager.h"
#include "Engine/Settings.h"
#include "Engine/TLSHolder.h"
#include "Engine/TimeLine.h"
#include "Engine/Transform.h"
#include "Engine/TrackMarker.h"
#include "Engine/ViewIdx.h"
#include "Engine/ViewerInstance.h"

#include "Serialization/CurveSerialization.h"
#include "Serialization/KnobSerialization.h"

#include "Engine/EngineFwd.h"

NATRON_NAMESPACE_ENTER

#define EXPR_RECURSION_LEVEL() ExprRecursionLevel_RAII __recursionLevelIncrementer__(this)

typedef std::map<ViewIdx, KnobDimViewBasePtr> PerViewKnobDataMap;
typedef std::vector<PerViewKnobDataMap> PerDimensionKnobDataMap;

typedef std::map<ViewIdx, KnobI::Expr> ExprPerViewMap;
typedef std::vector<ExprPerViewMap> ExprPerDimensionVec;

struct RedirectionLink
{
    KnobDimViewBasePtr savedData;
};
typedef std::map<ViewIdx, RedirectionLink> PerViewSavedDataMap;
typedef std::vector<PerViewSavedDataMap> PerDimensionSavedDataVec;


typedef std::map<ViewIdx, bool> PerViewHasModificationMap;
typedef std::vector<PerViewHasModificationMap> PerDimensionModificationMap;

typedef std::map<ViewIdx, bool> PerViewEnabledMap;
typedef std::vector<PerViewEnabledMap> PerDimensionEnabledMap;

typedef std::map<ViewIdx, bool> PerViewAllDimensionsVisible;

// Contains all datas shared amongst render clones and main instance
struct CommonData
{
    KnobFrameViewHashingStrategyEnum cacheInvalidationStrategy;

    // Protects the label
    mutable QMutex labelMutex;

    // The text label that will be displayed  on the GUI
    std::string label;

    // An icon to replace the label (one when checked, one when unchecked, for toggable buttons)
    std::string iconFilePath[2];

    // The script-name of the knob as available to python
    std::string name;

    // The original name passed to setName() by the user. The name might be different to comply to Python
    std::string originalName;

    // Should we add a new line after this parameter in the settings panel
    bool newLine;

    // Should we add a horizontal separator after this parameter
    bool addSeparator;

    // How much spacing in pixels should we add after this parameter. Only relevant if newLine is false
    int itemSpacing;

    // The spacing in pixels after this knob in the Viewer UI
    int inViewerContextItemSpacing;

    // The layout type in the viewer UI
    ViewerContextLayoutTypeEnum inViewerContextLayoutType;

    // The label in the viewer UI
    std::string inViewerContextLabel;

    // The icon in the viewer UI
    std::string inViewerContextIconFilePath[2];

    // Should this knob be available in the ShortCut editor by default?
    bool inViewerContextHasShortcut;

    // This is a list of script-names of knob shortcuts one can reference in the tooltip help.
    // See ViewerNode.cpp for an example.
    std::list<std::string> additionalShortcutsInTooltip;

    // A weak ptr to the parent knob containing this one. Each knob should be at least in a KnobPage
    // except the KnobPage itself.
    KnobIWPtr parentKnob;

    // Protects IsSecret, defaultIsSecret, enabled, inViewerContextSecret, defaultEnabled, evaluateOnChange
    mutable QMutex stateMutex;

    // Tells whether the knob is secret
    bool IsSecret;

    // Tells whether the knob is secret in the viewer. By default it is always visible in the viewer (if it has a viewer UI)
    bool inViewerContextSecret;

    // Is this parameter enabled
    bool enabled;

    // True if this knob can use the undo/redo stack
    bool CanUndo;

    // If true, a value change will never trigger an evaluation (render)
    bool evaluateOnChange;

    // If false this knob is not serialized into the project
    bool IsPersistent;

    // The hint tooltip displayed when hovering the mouse on the parameter
    std::string tooltipHint;

    // True if the hint contains markdown encoded data
    bool hintIsMarkdown;

    // True if this knob can receive animation curves
    bool isAnimationEnabled;

    // The number of dimensions in this knob (e.g: an RGBA KnobColor is 4-dimensional)
    int dimension;

    // For each view, a boolean indicating whether all dimensions are controled at once
    // protected by stateMutex
    PerViewAllDimensionsVisible allDimensionsVisible;

    // When true, autoFoldDimensions can be called to check if dimensions can be folded or not.
    bool autoFoldEnabled;

    // When true, autoAdjustFoldExpandDimensions can be called to automatically fold or expand dimensions
    bool autoAdjustFoldExpandEnabled;

    // protects perDimViewData and perDimViewSavedData
    mutable QMutex perDimViewDataMutex;

    // For each dimension and view the value stuff
    PerDimensionKnobDataMap perDimViewData;

    // When a dimension/view is linked to another knob, we save it so it can be restored
    // further on
    PerDimensionSavedDataVec perDimViewSavedData;

    // Was the knob declared by a plug-in or added by Natron?
    bool declaredByPlugin;

    // True if it was created by the user and should be put into the "User" page
    bool userKnob;

    // Pointer to a custom interact that should replace the UI of the knob
    OverlayInteractBasePtr customInteract;

    // Pointer to the knobGui interface if it has any
    KnobGuiIWPtr gui;


    // Protects expressions
    mutable QMutex expressionMutex;

    // For each dimension its expression
    ExprPerDimensionVec expressions;

    // Used to prevent expressions from creating infinite loops
    // It doesn't have to be thread-local even if getValue can be called on multiple threads:
    // the evaluation of expressions is locking-out all other threads anyway, so really a single
    // thread is using this variable at a time anyway.
    int expressionRecursionLevel;

    // Protects expressionRecursionLevel
    mutable QMutex expressionRecursionLevelMutex;

    // For each dimension, the label displayed on the interface (e.g: "R" "G" "B" "A")
    std::vector<std::string> dimensionNames;

    // Protects lastRandomHash
    mutable QMutex lastRandomHashMutex;

    // The last return value of random to preserve its state
    mutable U32 lastRandomHash;

    mutable QMutex renderClonesMapMutex;

    // For each instance, a pointer to the knob
    std::map<KnobHolderWPtr, KnobIWPtr> renderClonesMap;

    // Protects hasModifications
    mutable QMutex hasModificationsMutex;

    // For each dimension tells whether the knob is considered to have modification or not
    mutable PerDimensionModificationMap hasModifications;

    // Protects valueChangedBlocked
    mutable QMutex valueChangedBlockedMutex;

    // Recursive counter to prevent calls to knobChanged callback
    int valueChangedBlocked;

    // Recursive counter to prevent autokeying in setValue
    int autoKeyingDisabled;

    // If true, when this knob change, it is required to refresh the meta-data on a Node
    bool isMetadataSlave;
    
    // When enabled the keyframes can be displayed on the timeline if the knob is visible
    // protected by stateMutex
    bool keyframeTrackingEnabled;

    CommonData()
    : cacheInvalidationStrategy(eKnobHashingStrategyValue)
    , labelMutex()
    , label()
    , iconFilePath()
    , name()
    , originalName()
    , newLine(true)
    , addSeparator(false)
    , itemSpacing(0)
    , inViewerContextItemSpacing(5)
    , inViewerContextLayoutType(eViewerContextLayoutTypeSpacing)
    , inViewerContextLabel()
    , inViewerContextIconFilePath()
    , inViewerContextHasShortcut(false)
    , parentKnob()
    , stateMutex()
    , IsSecret(false)
    , inViewerContextSecret(false)
    , enabled(true)
    , CanUndo(true)
    , evaluateOnChange(true)
    , IsPersistent(true)
    , tooltipHint()
    , hintIsMarkdown(false)
    , isAnimationEnabled(true)
    , dimension()
    , allDimensionsVisible()
    , autoFoldEnabled(false)
    , autoAdjustFoldExpandEnabled(true)
    , perDimViewDataMutex()
    , perDimViewData()
    , perDimViewSavedData()
    , declaredByPlugin(true)
    , userKnob(false)
    , customInteract()
    , gui()
    , expressionMutex()
    , expressions()
    , expressionRecursionLevel(0)
    , expressionRecursionLevelMutex(QMutex::Recursive)
    , dimensionNames()
    , lastRandomHash(0)
    , renderClonesMapMutex()
    , renderClonesMap()
    , hasModificationsMutex()
    , hasModifications()
    , valueChangedBlockedMutex()
    , valueChangedBlocked(0)
    , autoKeyingDisabled(0)
    , isMetadataSlave(false)
    , keyframeTrackingEnabled(true)

    {

    }
};


struct KnobHelperPrivate
{
    // Ptr to the public class, can not be a smart ptr
    KnobHelper* publicInterface;

    // The holder containing this knob. This may be null if the knob is not in a collection
    KnobHolderWPtr holder;

    // Pointer to the main instance if this is a render clone, or NULL
    boost::weak_ptr<KnobHelper> mainInstance;

    boost::shared_ptr<CommonData> common;

    KnobHelperPrivate(KnobHelper* publicInterface_,
                      const KnobHolderPtr& holder_,
                      int nDims,
                      const std::string & scriptName)
    : publicInterface(publicInterface_)
    , holder(holder_)
    , mainInstance()
    , common(new CommonData)
    {
        common->dimension = nDims;
        common->name = scriptName;
        common->originalName = scriptName;
        common->label = scriptName;

        {
            KnobHolderPtr h = holder.lock();
            if ( h && !h->canKnobsAnimate() ) {
                common->isAnimationEnabled = false;
            }
        }

        common->dimensionNames.resize(nDims);
        common->hasModifications.resize(nDims);
        common->allDimensionsVisible[ViewIdx(0)] = true;
        common->expressions.resize(nDims);
        common->perDimViewSavedData.resize(nDims);
        for (int i = 0; i < nDims; ++i) {
            common->hasModifications[i][ViewIdx(0)] = false;
        }

        common->perDimViewData.resize(nDims);
    }

    KnobHelperPrivate(KnobHelper* publicInterface,
                      const KnobHolderPtr& holder,
                      const KnobHelperPtr& mainInstance)
    : publicInterface(publicInterface)
    , holder(holder)
    , mainInstance(mainInstance)
    , common(mainInstance->_imp->common)
    {
        publicInterface->_signalSlotHandler = mainInstance->_signalSlotHandler;
        common->perDimViewData.resize(common->dimension);
    }

    void parseListenersFromExpression(DimIdx dimension, ViewIdx view);

    /**
     * @brief Returns a string to append to the expression script declaring all Python attributes referencing nodes, knobs etc...
     * that can be reached through the expression at the given dimension/view.
     * @param addTab, if true, the script should be indented by one tab
     **/
    std::string getReachablePythonAttributesForExpression(bool addTab, DimIdx dimension, ViewIdx view);


};


class KnobExpressionKey : public CacheEntryKeyBase
{
public:

    struct ShmData
    {
        U64 nodeTimeViewVariantHash;
        int dimension;

        ShmData()
        {

        }

        ShmData(U64 nodeTimeViewVariantHash,
                int dimension)
        : nodeTimeViewVariantHash(nodeTimeViewVariantHash)
        , dimension(dimension)
        {

        }
    };

    KnobExpressionKey(U64 nodeTimeViewVariantHash,
                      int dimension,
                      const std::string& knobScriptName)
    : _data(nodeTimeViewVariantHash, dimension)
    , _knobScriptName(knobScriptName)
    {

    }

    virtual ~KnobExpressionKey()
    {

    }


    virtual int getUniqueID() const OVERRIDE FINAL
    {
        return kCacheKeyUniqueIDExpressionResult;
    }

    virtual void toMemorySegment(ExternalSegmentType* segment, ExternalSegmentTypeHandleList* objectPointers) const OVERRIDE FINAL
    {
        objectPointers->push_back(writeAnonymousSharedObject(_data, segment));
        objectPointers->push_back(writeAnonymousSharedObject(_knobScriptName,  segment));

    }

    virtual CacheEntryKeyBase::FromMemorySegmentRetCodeEnum fromMemorySegment(ExternalSegmentType* segment,
                                   ExternalSegmentTypeHandleList::const_iterator start,
                                   ExternalSegmentTypeHandleList::const_iterator end) OVERRIDE FINAL
    {
        if (start == end) {
            return eFromMemorySegmentRetCodeFailed;
        }
        readAnonymousSharedObject(*start, segment, &_data);
        ++start;
        if (start == end) {
            return eFromMemorySegmentRetCodeFailed;
        }
        readAnonymousSharedObject(*start, segment, &_knobScriptName);
    }

private:



    virtual void appendToHash(Hash64* hash) const OVERRIDE FINAL
    {
        hash->append(_data.nodeTimeViewVariantHash);
        hash->append(_data.dimension);
        Hash64::appendQString(QString::fromUtf8(_knobScriptName.c_str()), hash);
    }

    ShmData _data;
    std::string _knobScriptName;
};


class KnobExpressionResult : public CacheEntryBase
{
    KnobExpressionResult()
    : CacheEntryBase(appPTR->getGeneralPurposeCache())
    {

    }

public:

    enum KnobExpressionResultTypeEnum
    {
        eKnobExpressionResultTypePod,
        eKnobExpressionResultTypeString
    };

    static KnobExpressionResultPtr create(const KnobExpressionKeyPtr& key)
    {
        KnobExpressionResultPtr ret(new KnobExpressionResult());
        ret->setKey(key);
        return ret;
    }

    virtual ~KnobExpressionResult()
    {

    }

    // This is thread-safe and doesn't require a mutex:
    // The thread computing this entry and calling the setter is guaranteed
    // to be the only one interacting with this object. Then all objects
    // should call the getter.
    //
    void getResult(double* value, std::string* valueAsString) const
    {
        if (value) {
            *value = _valueResult;
        }
        if (valueAsString) {
            *valueAsString = _stringResult;
        }
    }

    void setResult(double value, const std::string& valueAsString)
    {
        _stringResult = valueAsString;
        _valueResult = value;
    }


    virtual void toMemorySegment(ExternalSegmentType* segment, ExternalSegmentTypeHandleList* objectPointers) const OVERRIDE FINAL
    {
        if (!_stringResult.empty()) {
            KnobExpressionResultTypeEnum type = eKnobExpressionResultTypeString;
            objectPointers->push_back(writeAnonymousSharedObject((int)type, segment));
            objectPointers->push_back(writeAnonymousSharedObject(_stringResult, segment));
        } else {
            KnobExpressionResultTypeEnum type = eKnobExpressionResultTypePod;
            objectPointers->push_back(writeAnonymousSharedObject((int)type, segment));
            objectPointers->push_back(writeAnonymousSharedObject(_valueResult, segment));
        }
        CacheEntryBase::toMemorySegment(segment, objectPointers);
    }

    virtual CacheEntryBase::FromMemorySegmentRetCodeEnum fromMemorySegment(bool isLockedForWriting,
                                                                           ExternalSegmentType* segment,
                                                                           ExternalSegmentTypeHandleList::const_iterator start,
                                                                           ExternalSegmentTypeHandleList::const_iterator end) OVERRIDE FINAL
    {
        int type_i;
        if (start == end) {
            throw std::bad_alloc();
        }
        readAnonymousSharedObject(*start, segment, &type_i);
        ++start;
        if (start == end) {
            throw std::bad_alloc();
        }
        switch ((KnobExpressionResultTypeEnum)type_i) {
            case eKnobExpressionResultTypePod: {
                readAnonymousSharedObject(*start, segment, &_valueResult);
            }   break;
            case eKnobExpressionResultTypeString: {
                readAnonymousSharedObject(*start, segment, &_stringResult);
            }   break;
        }
        ++start;
        return CacheEntryBase::fromMemorySegment(isLockedForWriting, segment, start, end);
    }

private:
    
    std::string _stringResult;
    double _valueResult;

};


NATRON_NAMESPACE_EXIT

#endif // KNOBPRIVATE_H
