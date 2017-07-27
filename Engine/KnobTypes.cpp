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

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "KnobTypes.h"

#include <cfloat>
#include <locale>
#include <sstream>
#include <algorithm> // min, max
#include <cassert>
#include <stdexcept>
#include <sstream> // stringstream

#if !defined(SBK_RUN) && !defined(Q_MOC_RUN)
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_OFF
#include <boost/math/special_functions/fpclassify.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_ON
#endif

#include <QtCore/QDebug>
#include <QtCore/QThread>
#include <QtCore/QCoreApplication>

#include "Engine/AppInstance.h"
#include "Engine/Bezier.h"
#include "Engine/BezierCP.h"
#include "Engine/Curve.h"
#include "Engine/ColorParser.h"
#include "Engine/EffectInstance.h"
#include "Engine/Format.h"
#include "Engine/Image.h"
#include "Engine/KnobFile.h"
#include "Engine/Hash64.h"
#include "Engine/Lut.h"
#include "Engine/Node.h"
#include "Engine/Project.h"
#include "Engine/TimeLine.h"
#include "Engine/Transform.h"
#include "Engine/ViewIdx.h"

#include "Serialization/CurveSerialization.h"
#include "Serialization/KnobSerialization.h"


NATRON_NAMESPACE_ENTER


struct KnobIntPrivate
{
    std::vector<int> increments;
    bool disableSlider;
    bool isRectangle;
    bool isValueCenteredInSpinbox;
    bool isShortcutKnob;

    KnobIntPrivate(int dimension)
    : increments(dimension, 1)
    , disableSlider(false)
    , isRectangle(false)
    , isValueCenteredInSpinbox(false)
    , isShortcutKnob(false)
    {

    }
};

KnobInt::KnobInt(const KnobHolderPtr& holder,
                 const std::string &name,
                 int dimension)
: KnobIntBase(holder, name, dimension)
, _imp(new KnobIntPrivate(dimension))
{
}


KnobInt::KnobInt(const KnobHolderPtr& holder, const KnobIPtr& mainKnob)
: KnobIntBase(holder, mainKnob)
, _imp(toKnobInt(mainKnob)->_imp)
{

}

KnobInt::~KnobInt()
{
    
}

void
KnobInt::disableSlider()
{
    _imp->disableSlider = true;
}

bool
KnobInt::isSliderDisabled() const
{
    return _imp->disableSlider;
}

void
KnobInt::setAsRectangle()
{
    if (getNDimensions() == 4) {
        _imp->isRectangle = true;
        disableSlider();
    }
}

bool
KnobInt::isRectangle() const
{
    return _imp->isRectangle;
}

void
KnobInt::setValueCenteredInSpinBox(bool enabled)
{
    _imp->isValueCenteredInSpinbox = enabled;
}

bool
KnobInt::isValueCenteredInSpinBox() const
{
    return _imp->isValueCenteredInSpinbox;
}

// For 2D int parameters, the UI will have a keybind recorder
// and the first dimension stores the symbol and the 2nd the modifiers
void
KnobInt::setAsShortcutKnob(bool isShortcutKnob)
{
    _imp->isShortcutKnob = isShortcutKnob;
}

bool
KnobInt::isShortcutKnob() const
{
    return _imp->isShortcutKnob;
}

void
KnobInt::setIncrement(int incr,
                      DimIdx index)
{
    if (incr <= 0) {
        qDebug() << "Attempting to set the increment of an int param to a value lesser or equal to 0";

        return;
    }

    if ( index >= (int)_imp->increments.size() ) {
        throw std::runtime_error("KnobInt::setIncrement , dimension out of range");
    }
    _imp->increments[index] = incr;
    Q_EMIT incrementChanged(_imp->increments[index], index);
}

void
KnobInt::setIncrement(const std::vector<int> &incr)
{
    assert( (int)incr.size() == getNDimensions() );
    _imp->increments = incr;
    for (U32 i = 0; i < _imp->increments.size(); ++i) {
        if (_imp->increments[i] <= 0) {
            qDebug() << "Attempting to set the increment of an int param to a value lesser or equal to 0";
            continue;
        }
        Q_EMIT incrementChanged(_imp->increments[i], DimIdx(i));
    }
}

const std::vector<int> &
KnobInt::getIncrements() const
{
    return _imp->increments;
}

bool
KnobInt::canAnimate() const
{
    return true;
}

const std::string KnobInt::_typeNameStr(kKnobIntTypeName);
const std::string &
KnobInt::typeNameStatic()
{
    return _typeNameStr;
}

const std::string &
KnobInt::typeName() const
{
    return typeNameStatic();
}


KnobBool::KnobBool(const KnobHolderPtr& holder,
                   const std::string &name,
                   int dimension)
    : KnobBoolBase(holder, name, dimension)
{
}

KnobBool::KnobBool(const KnobHolderPtr& holder, const KnobIPtr& mainKnob)
: KnobBoolBase(holder, mainKnob)
{

}

bool
KnobBool::canAnimate() const
{
    return canAnimateStatic();
}

const std::string KnobBool::_typeNameStr(kKnobBoolTypeName);
const std::string &
KnobBool::typeNameStatic()
{
    return _typeNameStr;
}

const std::string &
KnobBool::typeName() const
{
    return typeNameStatic();
}


struct KnobDoublePrivate
{
    bool spatial;
    bool isRectangle;
    std::vector<double>  increments;
    std::vector<int> decimals;
    bool disableSlider;

    /// to support ofx deprecated normalizd params:
    /// the first and second dimensions of the double param( hence a pair ) have a normalized state.
    /// BY default they have eValueIsNormalizedNone
    /// if the double type is one of
    /// - kOfxParamDoubleTypeNormalisedX - normalised size wrt to the project's X dimension (1D only),
    /// - kOfxParamDoubleTypeNormalisedXAbsolute - normalised absolute position on the X axis (1D only)
    /// - kOfxParamDoubleTypeNormalisedY - normalised size wrt to the project's Y dimension(1D only),
    /// - kOfxParamDoubleTypeNormalisedYAbsolute - normalised absolute position on the Y axis (1D only)
    /// - kOfxParamDoubleTypeNormalisedXY - normalised to the project's X and Y size (2D only),
    /// - kOfxParamDoubleTypeNormalisedXYAbsolute - normalised to the projects X and Y size, and is an absolute position on the image plane,
    std::vector<ValueIsNormalizedEnum> valueIsNormalized;

    ///For double params respecting the kOfxParamCoordinatesNormalised
    ///This tells us that only the default value is stored normalized.
    ///This SHOULD NOT bet set for old deprecated < OpenFX 1.2 normalized parameters.
    bool defaultValuesAreNormalized;


    KnobDoublePrivate(int dimension)
    : spatial(false)
    , isRectangle(false)
    , increments(dimension, 1)
    , decimals(dimension, 2)
    , disableSlider(false)
    , valueIsNormalized(dimension, eValueIsNormalizedNone)
    , defaultValuesAreNormalized(false)
    {
        
    }
};


KnobDouble::KnobDouble(const KnobHolderPtr& holder,
                       const std::string &name,
                       int dimension)
: KnobDoubleBase(holder, name, dimension)
, _imp(new KnobDoublePrivate(dimension))
{
}

KnobDouble::KnobDouble(const KnobHolderPtr& holder, const KnobIPtr& mainKnob)
: KnobDoubleBase(holder, mainKnob)
, _imp(toKnobDouble(mainKnob)->_imp)
{

}

void
KnobDouble::disableSlider()
{
    _imp->disableSlider = true;
}

bool
KnobDouble::isSliderDisabled() const
{
    return _imp->disableSlider;
}

bool
KnobDouble::canAnimate() const
{
    return true;
}

bool
KnobDouble::getIsSpatial() const
{
    return _imp->spatial;
}

void
KnobDouble::setAsRectangle()
{
    if (getNDimensions() == 4) {
        _imp->isRectangle = true;
    }
}

bool
KnobDouble::isRectangle() const
{
    return _imp->isRectangle;
}

bool
KnobDouble::getDefaultValuesAreNormalized() const
{
    return _imp->defaultValuesAreNormalized;
}

ValueIsNormalizedEnum
KnobDouble::getValueIsNormalized(DimIdx dimension) const
{
    assert(dimension >= 0 && dimension < (int)_imp->valueIsNormalized.size());
    return _imp->valueIsNormalized[dimension];
}

void
KnobDouble::setValueIsNormalized(DimIdx dimension,
                          ValueIsNormalizedEnum state)
{
    if (dimension < 0 || dimension >= (int)_imp->valueIsNormalized.size()) {
        throw std::invalid_argument("KnobDouble::setValueIsNormalized: dimension out of range");
    }
    _imp->valueIsNormalized[dimension] = state;

}

void
KnobDouble::setDefaultValuesAreNormalized(bool normalized)
{
    _imp->defaultValuesAreNormalized = normalized;
}

void
KnobDouble::setSpatial(bool spatial)
{
    _imp->spatial = spatial;
}


const std::string KnobDouble::_typeNameStr(kKnobDoubleTypeName);
const std::string &
KnobDouble::typeNameStatic()
{
    return _typeNameStr;
}

const std::string &
KnobDouble::typeName() const
{
    return typeNameStatic();
}

const std::vector<double> &
KnobDouble::getIncrements() const
{
    return _imp->increments;
}

const std::vector<int> &
KnobDouble::getDecimals() const
{
    return _imp->decimals;
}

void
KnobDouble::setIncrement(double incr,
                         DimIdx index)
{
    if (incr <= 0.) {
        qDebug() << "Attempting to set the increment of a double param to a value lesser or equal to 0.";

        return;
    }
    if ( index >= (int)_imp->increments.size() ) {
        throw std::runtime_error("KnobDouble::setIncrement , dimension out of range");
    }

    _imp->increments[index] = incr;
    Q_EMIT incrementChanged(_imp->increments[index], index);
}

void
KnobDouble::setDecimals(int decis,
                        DimIdx index)
{
    if ( index >= (int)_imp->decimals.size() ) {
        throw std::runtime_error("KnobDouble::setDecimals , dimension out of range");
    }

    _imp->decimals[index] = decis;
    Q_EMIT decimalsChanged(_imp->decimals[index], index);
}

void
KnobDouble::setIncrement(const std::vector<double> &incr)
{
    assert( incr.size() == (U32)getNDimensions() );
    _imp->increments = incr;
    for (U32 i = 0; i < incr.size(); ++i) {
        Q_EMIT incrementChanged(_imp->increments[i], DimIdx(i));
    }
}

void
KnobDouble::setDecimals(const std::vector<int> &decis)
{
    assert( decis.size() == (U32)getNDimensions() );
    _imp->decimals = decis;
    for (U32 i = 0; i < decis.size(); ++i) {
        Q_EMIT decimalsChanged(decis[i], DimIdx(i));
    }
}

KnobDouble::~KnobDouble()
{
}

static void
getNormalizeRect(const EffectInstancePtr& effect,
            TimeValue /*time*/,
            RectD & rod)
{
#ifdef NATRON_NORMALIZE_SPATIAL_WITH_ROD
    RenderScale scale;
    scale.y = scale.x = 1.;
    Status stat = effect->getRegionOfDefinition_public(0, time, scale, /*view*/ 0, &rod);
    if ( (stat == StatFailed) || ( (rod.x1 == 0) && (rod.y1 == 0) && (rod.x2 == 1) && (rod.y2 == 1) ) ) {
        Format f;
        effect->getRenderFormat(&f);
        rod = f;
    }
#else
    Format f;
    effect->getApp()->getProject()->getProjectDefaultFormat(&f);
    rod = f.toCanonicalFormat();
#endif
}

double
KnobDouble::denormalize(DimIdx dimension,
                        TimeValue time,
                        double value) const
{
    EffectInstancePtr effect = toEffectInstance( getHolder() );
    if (!effect) {
        // coverity[dead_error_line]
        return value;
    }
    RectD rod;
    getNormalizeRect(effect, time, rod);
    ValueIsNormalizedEnum e = getValueIsNormalized(dimension);
    // the second expression (with e == eValueIsNormalizedNone) is used when denormalizing default values
    if ( (e == eValueIsNormalizedX) || ( (e == eValueIsNormalizedNone) && (dimension == 0) ) ) {
        return value * rod.width();
    } else if ( (e == eValueIsNormalizedY) || ( (e == eValueIsNormalizedNone) && (dimension == 1) ) ) {
        return value * rod.height();
    }

    return value;
}

double
KnobDouble::normalize(DimIdx dimension,
                     TimeValue time,
                     double value) const
{
    EffectInstancePtr effect = toEffectInstance( getHolder() );

    assert(effect);
    if (!effect) {
        // coverity[dead_error_line]
        return value;
    }
    RectD rod;
    getNormalizeRect(effect, time, rod);
    ValueIsNormalizedEnum e = getValueIsNormalized(dimension);
    // the second expression (with e == eValueIsNormalizedNone) is used when normalizing default values
    if ( (e == eValueIsNormalizedX) || ( (e == eValueIsNormalizedNone) && (dimension == 0) ) ) {
        return value / rod.width();
    } else if ( (e == eValueIsNormalizedY) || ( (e == eValueIsNormalizedNone) && (dimension == 1) ) ) {
        return value / rod.height();
    }

    return value;
}

bool
KnobDouble::hasModificationsVirtual(const KnobDimViewBasePtr& data, DimIdx dimension) const
{
    if (Knob<double>::hasModificationsVirtual(data, dimension)) {
        return true;
    }

    ValueKnobDimView<double>* doubleData = dynamic_cast<ValueKnobDimView<double>*>(data.get());
    assert(doubleData);

    double defaultValue = getDefaultValue(dimension);
    if (_imp->defaultValuesAreNormalized) {
        double denormalizedDefaultValue = denormalize(dimension, TimeValue(0), defaultValue);
        QMutexLocker k(&doubleData->valueMutex);
        return doubleData->value != denormalizedDefaultValue;
    } else {
        QMutexLocker k(&doubleData->valueMutex);
        return doubleData->value != defaultValue;
    }
}


struct KnobButtonPrivate
{
    bool renderButton;
    bool checkable;
    bool isToolButtonAction;

    KnobButtonPrivate()
    : renderButton(false)
    , checkable(false)
    , isToolButtonAction(false)
    {

    }

};

KnobButton::KnobButton(const KnobHolderPtr& holder,
                       const std::string &name,
                       int dimension)
: KnobBoolBase(holder, name, dimension)
, _imp(new KnobButtonPrivate())
{
    //setIsPersistent(false);
}

KnobButton::KnobButton(const KnobHolderPtr& holder, const KnobIPtr& mainKnob)
: KnobBoolBase(holder, mainKnob)
, _imp(toKnobButton(mainKnob)->_imp)
{

}

KnobButton::~KnobButton()
{
    
}

void
KnobButton::setAsRenderButton()
{
    _imp->renderButton = true;
}

bool
KnobButton::isRenderButton() const
{
    return _imp->renderButton;
}

void
KnobButton::setCheckable(bool b)
{
    _imp->checkable = b;
}

bool
KnobButton::getIsCheckable() const
{
    return _imp->checkable;
}

void
KnobButton::setAsToolButtonAction(bool b)
{
    _imp->isToolButtonAction = b;
}

bool
KnobButton::getIsToolButtonAction() const
{
    return _imp->isToolButtonAction;
}

bool
KnobButton::canAnimate() const
{
    return false;
}

const std::string KnobButton::_typeNameStr(kKnobButtonTypeName);
const std::string &
KnobButton::typeNameStatic()
{
    return _typeNameStr;
}

const std::string &
KnobButton::typeName() const
{
    return typeNameStatic();
}

bool
KnobButton::trigger()
{
    return evaluateValueChange(DimSpec(0), getCurrentRenderTime(), ViewSetSpec(0),  eValueChangedReasonUserEdited);
}

/******************************KnobChoice**************************************/

// don't show help in the tootlip if there are more entries that this
#define KNOBCHOICE_MAX_ENTRIES_HELP 40

ChoiceKnobDimView::ChoiceKnobDimView()
: ValueKnobDimView<int>()
, menuOptions()
, separators()
, shortcuts()
, menuIcons()
, addNewChoiceCallback(0)
, textToFitHorizontally()
, isCascading(false)
, showMissingEntryWarning(true)
, menuColors()
{

}

ValueChangedReturnCodeEnum
ChoiceKnobDimView::setValueAtTime(TimeValue time, const int& value, KeyFrame* newKey)
{
    if (!animationCurve) {
        return eValueChangedReturnCodeNothingChanged;
    }
    // check for infinity
    if (boost::math::isinf(value)) {
        *newKey = KeyFrame( (double)time, 0. );
    } else {
        *newKey = KeyFrame( (double)time, value );
    }

    ChoiceOption activeEntry;
    if (value >= 0 && value < (int)menuOptions.size()) {
        activeEntry = menuOptions[value];
    }

    newKey->setProperty(kKeyframePropChoiceOptionID, activeEntry.id);
    newKey->setProperty(kKeyframePropChoiceOptionLabel, activeEntry.label);

    ValueChangedReturnCodeEnum addKeyRet = animationCurve->setOrAddKeyframe(*newKey);
    notifyCurveChanged();

    return addKeyRet;
}

bool
ChoiceKnobDimView::setValueAndCheckIfChanged(const int& v)
{
    bool changed = ValueKnobDimView<int>::setValueAndCheckIfChanged(v);

    QMutexLocker k(&valueMutex);
    ChoiceOption newChoice;
    if (v >= 0 && v < (int)menuOptions.size()) {
        newChoice = menuOptions[v];
    } else {
        // No current value, assume they are different
        return true;
    }
    if (staticValueOption.id != newChoice.id) {
        staticValueOption = newChoice;
        return true;
    }
    return changed;
}

bool
ChoiceKnobDimView::copy(const CopyInArgs& inArgs, CopyOutArgs* outArgs)
{
    bool hasChanged = ValueKnobDimView<int>::copy(inArgs, outArgs);

    const ChoiceKnobDimView* otherType = dynamic_cast<const ChoiceKnobDimView*>(inArgs.other);
    assert(otherType);

    QMutexLocker k(&valueMutex);
    QMutexLocker k2(&inArgs.other->valueMutex);

    menuOptions = otherType->menuOptions;
    separators = otherType->separators;
    shortcuts = otherType->shortcuts;
    menuIcons = otherType->menuIcons;
    addNewChoiceCallback = otherType->addNewChoiceCallback;
    textToFitHorizontally = otherType->textToFitHorizontally;
    isCascading = otherType->isCascading;
    showMissingEntryWarning = otherType->showMissingEntryWarning;
    menuColors = otherType->menuColors;

    if (staticValueOption.id != otherType->staticValueOption.id) {
        staticValueOption = otherType->staticValueOption;
        hasChanged = true;
    }

    return hasChanged;
}

struct KnobChoicePrivate {

    // The default value as a string
    mutable QMutex defaultEntryMutex;
    std::string initialDefaultEntryID, defaultEntryID;

    KnobChoicePrivate()
    : defaultEntryMutex()
    , initialDefaultEntryID()
    , defaultEntryID()
    {

    }
};

KnobChoice::KnobChoice(const KnobHolderPtr& holder,
                       const std::string &name,
                       int nDims)
: KnobIntBase(holder, name, nDims)
, _imp(new KnobChoicePrivate)
{
}

KnobChoice::KnobChoice(const KnobHolderPtr& holder, const KnobIPtr& mainInstance)
: KnobIntBase(holder, mainInstance)
, _imp(new KnobChoicePrivate)
{

}

KnobChoice::~KnobChoice()
{
}

void
KnobChoice::setMissingEntryWarningEnabled(bool enabled)
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    data->showMissingEntryWarning = enabled;
}

bool
KnobChoice::isMissingEntryWarningEnabled() const
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    return data->showMissingEntryWarning;
}

void
KnobChoice::setColorForIndex(int index, const RGBAColourD& color)
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    data->menuColors[index] = color;
}

bool
KnobChoice::getColorForIndex(int index, RGBAColourD* color) const
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    std::map<int, RGBAColourD>::const_iterator found = data->menuColors.find(index);
    if (found == data->menuColors.end()) {
        return false;
    }
    *color = found->second;
    return true;
}

void
KnobChoice::setTextToFitHorizontally(const std::string& text)
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    data->textToFitHorizontally = text;
}

std::string
KnobChoice::getTextToFitHorizontally() const
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    return data->textToFitHorizontally;
}

void
KnobChoice::setNewOptionCallback(ChoiceKnobDimView::KnobChoiceNewItemCallback callback)
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    data->addNewChoiceCallback = callback;
}

ChoiceKnobDimView::KnobChoiceNewItemCallback
KnobChoice::getNewOptionCallback() const
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    return data->addNewChoiceCallback;
}

void
KnobChoice::setCascading(bool cascading)
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    data->isCascading = cascading;
}

bool
KnobChoice::isCascading() const
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    return data->isCascading;
}

void
KnobChoice::onLinkChanged()
{
    // We changed data, refresh the menu
    Q_EMIT populated();
}

bool
KnobChoice::canLinkWith(const KnobIPtr & other, DimIdx thisDimension, ViewIdx thisView, DimIdx otherDim, ViewIdx otherView, std::string* error) const
{
    if (!Knob<int>::canLinkWith(other, thisDimension, thisView, otherDim, otherView, error)) {
        return false;
    }
    KnobChoice* otherIsChoice = dynamic_cast<KnobChoice*>(other.get());
    if (!otherIsChoice) {
        if (error) {
            *error = tr("You can only copy/paste between parameters of the same type. To overcome this, use an expression instead.").toStdString();
        }
        return false;
    }
    ChoiceKnobDimViewPtr otherData = toChoiceKnobDimView(otherIsChoice->getDataForDimView(otherDim, otherView));
    ChoiceKnobDimViewPtr thisData = toChoiceKnobDimView(getDataForDimView(thisDimension, thisView));
    assert(otherData && thisData);
    if ( !otherData || !thisData) {
        return false;
    }

    // Choice parameters with different menus cannot be linked
    QString menuDifferentError = tr("You cannot link choice parameters with different menus. To overcome this, use an expression instead.");
    std::vector<ChoiceOption> thisOptions, otherOptions;
    {
        QMutexLocker k(&thisData->valueMutex);
        thisOptions = thisData->menuOptions;
    }
    {
        QMutexLocker k(&otherData->valueMutex);
        otherOptions = otherData->menuOptions;
    }
    if (thisOptions.size() != otherOptions.size()) {
        *error = menuDifferentError.toStdString();
        return false;
    }
    for (std::size_t i = 0; i < thisOptions.size(); ++i) {
        if (thisOptions[i].id != otherOptions[i].id) {
            *error = menuDifferentError.toStdString();
            return false;
        }
    }
    return true;
} // canLinkWith


bool
KnobChoice::canAnimate() const
{
    return canAnimateStatic();
}

const std::string KnobChoice::_typeNameStr(kKnobChoiceTypeName);
const std::string &
KnobChoice::typeNameStatic()
{
    return _typeNameStr;
}

const std::string &
KnobChoice::typeName() const
{
    return typeNameStatic();
}


bool
KnobChoice::hasModificationsVirtual(const KnobDimViewBasePtr& data, DimIdx dimension) const
{
    if (Knob<int>::hasModificationsVirtual(data, dimension)) {
        return true;
    }

    std::string defaultVal;
    {
        QMutexLocker k(&_imp->defaultEntryMutex);
        defaultVal = _imp->defaultEntryID;
    }


    ChoiceKnobDimViewPtr choiceData = toChoiceKnobDimView(data);
    assert(choiceData);
    QMutexLocker k(&data->valueMutex);

    if (choiceData->staticValueOption.id != defaultVal) {
        return true;
    }

    return false;
}



void
KnobChoice::findAndSetOldChoice()
{
    std::list<ViewIdx> views = getViewsList();
    if (views.empty()) {
        return;
    }


    // Also ensure the default index is correct wrt the new chocies
    std::string defChoiceID = getDefaultEntryID();

    for (std::list<ViewIdx>::const_iterator it = views.begin(); it != views.end(); ++it) {

        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), *it));
        assert(data);

        int found = -1;
        int foundDefValue = -1;
        {
            QMutexLocker k(&data->valueMutex);

            for (std::size_t i = 0; i < data->menuOptions.size(); ++i) {
                if ( !data->staticValueOption.id.empty() && data->menuOptions[i].id == data->staticValueOption.id ) {
                    // Refresh label and hint, even if ID is the same
                    data->staticValueOption = data->menuOptions[i];
                    found = i;
                }
                if (!defChoiceID.empty() && data->menuOptions[i].id == defChoiceID) {
                    foundDefValue = i;
                }
                if (foundDefValue != -1 && found != -1) {
                    break;
                }
            }

        }

        if (foundDefValue != -1) {
            int defIndex = getDefaultValue(DimIdx(0));
            int curIndex = getValue();
            if (foundDefValue != defIndex) {
                setDefaultValueWithoutApplying(foundDefValue);
                // If this is the first time we call populateChoices
                // the default index might not be the correct one, ensure it is valid.
                if (curIndex == defIndex) {
                    // Find the index of the default entry and update the default index
                    // unless we found the entry (found != -1)
                    if (found == -1) {
                        setValue(foundDefValue);
                        return;
                    }
                }
            }
        }


        if (found != -1) {
            // Make sure we don't call knobChanged if we found the value
            blockValueChanges();
            ScopedChanges_RAII changes(this);
            setValue(found, ViewSetSpec(*it));
            unblockValueChanges();
        }
    } // for all views

}

bool
KnobChoice::populateChoices(const std::vector<ChoiceOption> &entries)
{
    KnobDimViewKeySet sharedKnobs;

    bool mustSetDefaultEntry = false;
    std::string defaultEntryID;
    {
        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
        assert(data);

        {
            // Check if the default value string is empty, if so initialize it
            if (isDefaultValueSet(DimIdx(0))) {
                QMutexLocker k2(&_imp->defaultEntryMutex);
                if (_imp->initialDefaultEntryID.empty()) {
                    mustSetDefaultEntry = true;
                }
            }
        }

        int defValueIndex = getDefaultValue(DimIdx(0));
        if (mustSetDefaultEntry) {
            // The default entry ID was not set yet, set it from the index.
            if (defValueIndex >= 0 && defValueIndex < (int)entries.size()) {
                defaultEntryID = entries[defValueIndex].id;
            }
        }
        
        
        QMutexLocker k(&data->valueMutex);
        sharedKnobs = data->sharedKnobs;

        data->menuOptions = entries;
        for (std::size_t i = 0; i < data->menuOptions.size(); ++i) {

            // The ID cannot be empty, this is the only way to uniquely identify the choice.
            assert(!data->menuOptions[i].id.empty());

            // If the label is not set, use the ID
            if (data->menuOptions[i].label.empty()) {
                data->menuOptions[i].label = data->menuOptions[i].id;
            }
        }

    } // QMutexLocker

    if (mustSetDefaultEntry) {
         QMutexLocker k2(&_imp->defaultEntryMutex);
        _imp->initialDefaultEntryID = defaultEntryID;
        _imp->defaultEntryID = defaultEntryID;
    }


    //  Try to restore the last choice.
    findAndSetOldChoice();

    for (KnobDimViewKeySet::const_iterator it = sharedKnobs.begin(); it!=sharedKnobs.end(); ++it) {
        KnobChoicePtr sharedKnob = toKnobChoice(it->knob.lock());
        assert(sharedKnob);
        if (!sharedKnob) {
            continue;
        }
        // Notify tooltip changed because we changed the menu entries
        sharedKnob->_signalSlotHandler->s_helpChanged();

        Q_EMIT sharedKnob->populated();
    }



    return true;
} // KnobChoice::populateChoices


void
KnobChoice::setShortcuts(const std::map<int, std::string>& shortcuts)
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    data->shortcuts = shortcuts;
}

std::map<int, std::string>
KnobChoice::getShortcuts() const
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    return data->shortcuts;
}

void
KnobChoice::setIcons(const std::map<int, std::string>& icons)
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    data->menuIcons = icons;

}

std::map<int, std::string>
KnobChoice::getIcons() const
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    return data->menuIcons;
}

void
KnobChoice::setSeparators(const std::vector<int>& separators)
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    data->separators = separators;
}

std::vector<int>
KnobChoice::getSeparators() const
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);
    return data->separators;
}


void
KnobChoice::resetChoices(ViewSetSpec view)
{
    std::list<ViewIdx> views = getViewsList();
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            ViewIdx view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
            if (view_i != *it) {
                continue;
            }
        }

        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), *it));
        if (!data) {
            continue;
        }
        KnobDimViewKeySet sharedKnobs;
        {
            QMutexLocker k(&data->valueMutex);
            sharedKnobs = data->sharedKnobs;
            data->menuOptions.clear();
        }

        for (KnobDimViewKeySet::const_iterator it = sharedKnobs.begin(); it!=sharedKnobs.end(); ++it) {
            KnobChoicePtr sharedKnob = toKnobChoice(it->knob.lock());
            assert(sharedKnob);
            if (!sharedKnob) {
                continue;
            }
            // Notify tooltip changed because we changed the menu entries
            sharedKnob->_signalSlotHandler->s_helpChanged();

            Q_EMIT sharedKnob->entriesReset();
        }

    }

    // Refresh active entry state
    findAndSetOldChoice();


}

void
KnobChoice::appendChoice(const ChoiceOption& option,
                         ViewSetSpec view)
{
    // The ID is the only way to uniquely identify the option! It must be set.
    assert(!option.id.empty());

    std::list<ViewIdx> views = getViewsList();
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            ViewIdx view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
            if (view_i != *it) {
                continue;
            }
        }

        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), *it));
        if (!data) {
            continue;
        }
        KnobDimViewKeySet sharedKnobs;
        {
            QMutexLocker k(&data->valueMutex);
            data->menuOptions.push_back(option);
            ChoiceOption& copiedOption = data->menuOptions.back();

            // If label is empty, set to the option
            if (copiedOption.label.empty()) {
                copiedOption.label = copiedOption.id;
            }
            sharedKnobs = data->sharedKnobs;
        }
        for (KnobDimViewKeySet::const_iterator it = sharedKnobs.begin(); it!=sharedKnobs.end(); ++it) {
            KnobChoicePtr sharedKnob = toKnobChoice(it->knob.lock());
            assert(sharedKnob);
            if (!sharedKnob) {
                continue;
            }
            // Notify tooltip changed because we changed the menu entries
            sharedKnob->_signalSlotHandler->s_helpChanged();

            Q_EMIT sharedKnob->entryAppended();
        }
    }

    // Refresh active entry state
    findAndSetOldChoice();
}

std::vector<ChoiceOption>
KnobChoice::getEntries(ViewIdx view) const
{
    ViewIdx view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    {
        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), view_i));
        if (!data) {
            return std::vector<ChoiceOption>();
        }
        QMutexLocker k(&data->valueMutex);
        return data->menuOptions;
    }

}

bool
KnobChoice::isActiveEntryPresentInEntries(ViewIdx view) const
{

    {
        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), view));
        if (!data) {
            return false;
        }
        QMutexLocker k(&data->valueMutex);
        for (std::size_t i = 0; i < data->menuOptions.size(); ++i) {
            if (data->menuOptions[i].id == data->staticValueOption.id) {
                return true;
            }
        }

    }
    return false;
}

ChoiceOption
KnobChoice::getEntry(int v, ViewIdx view) const
{
    ViewIdx view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    {
        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), view_i));
        if (!data) {
            return ChoiceOption("","","");
        }
        QMutexLocker k(&data->valueMutex);
        if (v < 0 || (int)data->menuOptions.size() <= v ) {
            throw std::invalid_argument( std::string("KnobChoice::getEntry: index out of range") );
        }
        return data->menuOptions[v];
    }
}

int
KnobChoice::getNumEntries(ViewIdx view) const
{
    ViewIdx view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    {
        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), view_i));
        if (!data) {
            return false;
        }
        QMutexLocker k(&data->valueMutex);
        return data->menuOptions.size();
    }
}



void
KnobChoice::setActiveEntry(const ChoiceOption& entry, ViewSetSpec view)
{

    std::list<ViewIdx> views = getViewsList();
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            ViewIdx view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
            if (view_i != *it) {
                continue;
            }
        }

        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), *it));
        if (!data) {
            continue;
        }

        KnobDimViewKeySet sharedKnobs;
        int matchedIndex;
        {

            QMutexLocker k(&data->valueMutex);
            ChoiceOption matchedEntry;
            matchedIndex = choiceMatch(entry.id, data->menuOptions, &matchedEntry);
            if (matchedIndex == -1) {
                matchedEntry = entry;
            }
            sharedKnobs = data->sharedKnobs;
            data->staticValueOption = matchedEntry;
        }
        if (matchedIndex != -1) {
            setValue(matchedIndex);
        }
        for (KnobDimViewKeySet::const_iterator it = sharedKnobs.begin(); it!=sharedKnobs.end(); ++it) {
            KnobChoicePtr sharedKnob = toKnobChoice(it->knob.lock());
            assert(sharedKnob);
            if (!sharedKnob) {
                continue;
            }

            Q_EMIT sharedKnob->populated();
        }

    }
    computeHasModifications();
}

ChoiceOption
KnobChoice::getCurrentEntry(ViewIdx view)
{
    return getCurrentEntryAtTime(getCurrentRenderTime(), view);
}

ChoiceOption
KnobChoice::getCurrentEntryAtTime(TimeValue time, ViewIdx view)
{
    ViewIdx view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    {
        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), view_i));
        if (!data) {
            return ChoiceOption();
        }
        ChoiceOption ret;

        {
            QMutexLocker k(&data->valueMutex);

            if (data->animationCurve && data->animationCurve->isAnimated()) {
                KeyFrame key = data->animationCurve->getValueAt(time);
                bool gotIt = key.getPropertySafe(kKeyframePropChoiceOptionID, 0, &ret.id);
                assert(gotIt);
                gotIt = key.getPropertySafe(kKeyframePropChoiceOptionLabel, 0, &ret.id);
                assert(gotIt);
            } else {
                ret = data->staticValueOption;
            }
        }

        if (!ret.id.empty()) {
            return ret;
        }

        // Active entry was not set yet, give something based on the index and set the active entry
        int activeIndex = getValueAtTime(time, DimIdx(0), view_i);
        {
            QMutexLocker k(&data->valueMutex);
            if ( activeIndex >= 0 && activeIndex < (int)data->menuOptions.size() ) {
                data->staticValueOption = data->menuOptions[activeIndex];
                return data->staticValueOption;
            }

        }
    }

    return ChoiceOption();
}



std::string
KnobChoice::getHintToolTipFull() const
{
    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    QMutexLocker k(&data->valueMutex);

    int gothelp = 0;
    // list values that either have help or have label != id
    if ( !data->menuOptions.empty() ) {
        for (std::size_t i = 0; i < data->menuOptions.size(); ++i) {
            if ( (data->menuOptions[i].id != data->menuOptions[i].label) || !data->menuOptions[i].tooltip.empty() ) {
                ++gothelp;
            }
        }
    }

    if (gothelp > KNOBCHOICE_MAX_ENTRIES_HELP) {
        // too many entries
        gothelp = 0;
    }
    std::stringstream ss;
    if ( !getHintToolTip().empty() ) {
        ss << boost::trim_copy( getHintToolTip() );
        if (gothelp) {
            // if there are per-option help strings, separate them from main hint
            ss << "\n\n";
        }
    }
    // param may have no hint but still have per-option help
    if (gothelp) {
        for (std::size_t i = 0; i < data->menuOptions.size(); ++i) {
            if ( !data->menuOptions[i].tooltip.empty() || data->menuOptions[i].id != data->menuOptions[i].label ) { // no help line is needed if help is unavailable for this option
                std::string entry = boost::trim_copy(data->menuOptions[i].label);
                std::replace_if(entry.begin(), entry.end(), ::isspace, ' ');
                if (data->menuOptions[i].label != data->menuOptions[i].id) {
                    entry += "  (" + data->menuOptions[i].id + ")";
                }
                std::string help = boost::trim_copy(data->menuOptions[i].tooltip);
                std::replace_if(help.begin(), help.end(), ::isspace, ' ');
                if ( isHintInMarkdown() ) {
                    ss << "* **" << entry << "**";
                } else {
                    ss << entry;
                }
                if (!data->menuOptions[i].tooltip.empty()) {
                    ss << ": ";
                    ss << help;
                }
                if (i < data->menuOptions.size() - 1) {
                    ss << '\n';
                }
            }
        }
    }

    return ss.str();
} // KnobChoice::getHintToolTipFull

ValueChangedReturnCodeEnum
KnobChoice::setValueFromID(const std::string & value, ViewSetSpec view, ValueChangedReasonEnum reason)
{
    std::list<ViewIdx> views = getViewsList();
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            ViewIdx view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
            if (view_i != *it) {
                continue;
            }
        }


        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), *it));
        if (!data) {
            continue;
        }
        int index = -1;
        {
            QMutexLocker k(&data->valueMutex);
            index = choiceMatch(value, data->menuOptions, &data->staticValueOption);
        }
        if (index != -1) {
            return setValue(index, view, DimIdx(0), reason);
        }

    }

    return eValueChangedReturnCodeNothingChanged;
}

// try to match entry id first, then label
static
const std::string&
entryStr(const ChoiceOption& opt, int s)
{
    return s == 0 ? opt.id : opt.label;
}

static
bool BothAreSpaces(char lhs, char rhs) { return (lhs == rhs) && (lhs == ' '); }

// Choice restoration tries several options to restore a choice value:
// 1- exact string match, same index
// 2- exact string match, other index
// 3- exact string match before the first '\t', other index
// 4- case-insensistive string match, other index
// 5- paren/bracket-insensitive match (for WriteFFmpeg's format and codecs)
// 6- if the choice ends with " 1" try to match exactly everything before that  (for formats with PAR=1, where the PAR was removed)
// returns index if choice was matched, -1 if not matched
#pragma message WARN("choiceMatch() should be moved into filterKnobChoiceOptionCompat().")
// TODO: choiceMatch() should be moved into filterKnobChoiceOptionCompat()
// TODO: filterKnobChoiceOptionCompat() should be used everywhere instead of choiceMatch()
int
KnobChoice::choiceMatch(const std::string& choice,
                        const std::vector<ChoiceOption>& entries,
                        ChoiceOption* matchedEntry)
{
    // try to match entry id first, then label
    for (int s = 0; s < 2; ++s) {
        // 2- try exact match, other index
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entryStr(entries[i], s) == choice) {
                if (matchedEntry) {
                    *matchedEntry = entries[i];
                }
                return i;
            }
        }

        // 3- match the part before '\t' with the part before '\t'. This is for value-tab-description options such as in the WriteFFmpeg codec
        std::size_t choicetab = choice.find('\t'); // returns string::npos if no tab was found
        std::string choicemain = choice.substr(0, choicetab); // gives the entire string if no tabs were found
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const ChoiceOption& entry(entries[i]);
            std::size_t entrytab = entry.id.find('\t'); // returns string::npos if no tab was found
            std::string entrymain = entry.id.substr(0, entrytab); // gives the entire string if no tabs were found

            if (entrymain == choicemain) {
                if (matchedEntry) {
                    *matchedEntry = entries[i];
                }
                return i;
            }
        }

        // 4- case-insensitive match
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if ( boost::iequals(entryStr(entries[i], s), choice) ) {
                if (matchedEntry) {
                    *matchedEntry = entries[i];
                }
                return i;
            }
        }

        // 5- paren/bracket-insensitive match (for WriteFFmpeg's format and codecs, parameter names "format" and "codec" in fr.inria.openfx.WriteFFmpeg)
        std::string choiceparen = choice;
        std::replace( choiceparen.begin(), choiceparen.end(), '[', '(');
        std::replace( choiceparen.begin(), choiceparen.end(), ']', ')');
        for (std::size_t i = 0; i < entries.size(); ++i) {
            std::string entryparen = entryStr(entries[i], s);
            std::replace( entryparen.begin(), entryparen.end(), '[', '(');
            std::replace( entryparen.begin(), entryparen.end(), ']', ')');

            if (choiceparen == entryparen) {
                if (matchedEntry) {
                    *matchedEntry = entries[i];
                }
                return i;
            }
        }

        // 6- handle old format strings, like "square_256  256 x 256  1":
        // - remove duplicate spaces
        // - if the choice ends with " 1" try to match exactly everything before that  (for formats with par=1, where the PAR was removed)
        // - if the choice contains " x ", try to remove one space before and after the x
        // Note: the parameter name is "outputFormat" in project serialization
        {
            bool choiceformatfound = false;
            std::string choiceformat = boost::trim_copy(choice); // trim leading and trailing whitespace
            if (choiceformat != choice) {
                choiceformatfound = true;
            }
            if (choiceformat.find("  ") != std::string::npos) { // remove duplicate spaces
                std::string::iterator new_end = std::unique(choiceformat.begin(), choiceformat.end(), BothAreSpaces);
                choiceformat.erase(new_end, choiceformat.end());
                choiceformatfound = true;
            }
            if ( boost::algorithm::ends_with(choiceformat, " 1") ) { // remove " 1" at the end
                choiceformat.resize(choiceformat.size()-2);
                choiceformatfound = true;
            }
            if (choiceformat.find(" x ") != std::string::npos) { // remove spaces around 'x'
                boost::replace_first(choiceformat, " x ", "x");
                choiceformatfound = true;
            }
            if (choiceformatfound) {
                for (std::size_t i = 0; i < entries.size(); ++i) {
                    if (entryStr(entries[i], s) == choiceformat) {
                        if (matchedEntry) {
                            *matchedEntry = entries[i];
                        }
                        return i;

                    }
                }
            }
        }
    } // for s

    // no match
    return -1;
}

void
KnobChoice::setCurrentDefaultValueAsInitialValue()
{
    {
        QMutexLocker l(&_imp->defaultEntryMutex);
        _imp->initialDefaultEntryID = _imp->defaultEntryID;
    }
    KnobIntBase::setCurrentDefaultValueAsInitialValue();
}

std::string
KnobChoice::getDefaultEntryID() const
{
    {
        QMutexLocker l(&_imp->defaultEntryMutex);
        if (!_imp->defaultEntryID.empty()) {

            return _imp->defaultEntryID;
        }
    }
    int defIndex = getDefaultValue(DimIdx(0));
    {
        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
        if (!data) {
            return std::string();
        }
        QMutexLocker k(&data->valueMutex);
        if (defIndex < 0 || (int)data->menuOptions.size() <= defIndex ) {
            return std::string();
        }

        return data->menuOptions[defIndex].id;
    }
}

void
KnobChoice::onDefaultValueChanged(DimSpec /*dimension*/)
{
    int defIndex = getDefaultValue(DimIdx(0));

    ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    std::string optionID;
    {
        QMutexLocker k(&data->valueMutex);
        if (defIndex >= 0 && defIndex < (int)data->menuOptions.size()) {
            optionID = data->menuOptions[defIndex].id;
        }
    }
    if (optionID.empty()) {
        return;
    }

    QMutexLocker k2(&_imp->defaultEntryMutex);
    if (_imp->initialDefaultEntryID.empty()) {
        _imp->initialDefaultEntryID = optionID;
    }
    _imp->defaultEntryID = optionID;

}

bool
KnobChoice::hasDefaultValueChanged(DimIdx /*dimension*/) const
{
    QMutexLocker l(&_imp->defaultEntryMutex);
    return _imp->defaultEntryID != _imp->initialDefaultEntryID;
}

void
KnobChoice::setDefaultValueFromIDWithoutApplying(const std::string & value)
{
    {
        QMutexLocker k(&_imp->defaultEntryMutex);
        if (_imp->initialDefaultEntryID.empty()) {
            _imp->initialDefaultEntryID = value;
        }
        _imp->defaultEntryID = value;
    }
    int index = -1;
    {
        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
        assert(data);
        QMutexLocker k(&data->valueMutex);
        index = choiceMatch(value, data->menuOptions, 0);
    }
    if (index != -1) {
        return setDefaultValueWithoutApplying(index, DimSpec(0));
    }
}

void
KnobChoice::setDefaultValueFromID(const std::string & value)
{
    {
        QMutexLocker k(&_imp->defaultEntryMutex);
        if (_imp->initialDefaultEntryID.empty()) {
            _imp->initialDefaultEntryID = value;
        }
        _imp->defaultEntryID = value;
    }

    int index = -1;
    {
        ChoiceKnobDimViewPtr data = toChoiceKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
        assert(data);
        QMutexLocker k(&data->valueMutex);
        data->staticValueOption.id = value;
        index = KnobChoice::choiceMatch(value, data->menuOptions, 0);
    }
    if (index != -1) {
        return setDefaultValue(index, DimSpec(0));
    }
}

KnobDimViewBasePtr
KnobChoice::createDimViewData() const
{
    ChoiceKnobDimViewPtr ret(new ChoiceKnobDimView);
    return ret;
}


/******************************KnobSeparator**************************************/

KnobSeparator::KnobSeparator(const KnobHolderPtr& holder,
                             const std::string &name,
                             int dimension)
    : KnobBoolBase(holder, name, dimension)
{
}

KnobSeparator::KnobSeparator(const KnobHolderPtr& holder, const KnobIPtr& mainInstance)
: KnobBoolBase(holder, mainInstance)
{

}

bool
KnobSeparator::canAnimate() const
{
    return false;
}

const std::string KnobSeparator::_typeNameStr(kKnobSeparatorTypeName);
const std::string &
KnobSeparator::typeNameStatic()
{
    return _typeNameStr;
}

const std::string &
KnobSeparator::typeName() const
{
    return typeNameStatic();
}

/******************************KnobColor**************************************/

struct KnobColorPrivate
{
    bool simplifiedMode;

    // Color-space name (mapped to the ones in Lut.cpp, but could be changed for OCIO in the future)
    std::string uiColorspace, internalColorspace;

    KnobColorPrivate()
    : simplifiedMode(false)
    , uiColorspace(kColorKnobDefaultUIColorspaceName)
    , internalColorspace()
    {
        
    }
};

KnobColor::KnobColor(const KnobHolderPtr& holder,
                     const std::string &name,
                     int dimension)
: KnobDoubleBase(holder, name, dimension)
, _imp(new KnobColorPrivate())
{
    //dimension greater than 4 is not supported. Dimension 2 doesn't make sense.
    assert(dimension <= 4 && dimension != 2);
}

KnobColor::KnobColor(const KnobHolderPtr& holder, const KnobIPtr& mainInstance)
: KnobDoubleBase(holder, mainInstance)
, _imp(toKnobColor(mainInstance)->_imp)
{

}

KnobColor::~KnobColor()
{
    
}

bool
KnobColor::canAnimate() const
{
    return true;
}

const std::string
KnobColor::_typeNameStr(kKnobColorTypeName);
const std::string &
KnobColor::typeNameStatic()
{
    return _typeNameStr;
}

const std::string &
KnobColor::typeName() const
{
    return typeNameStatic();
}

void
KnobColor::setUIColorspaceName(const std::string& csName)
{
    _imp->uiColorspace = csName;
}

const std::string&
KnobColor::getUIColorspaceName() const
{
    return _imp->uiColorspace;
}

void
KnobColor::setInternalColorspaceName(const std::string& csName)
{
    _imp->internalColorspace = csName;
}

const std::string&
KnobColor::getInternalColorspaceName() const
{
    return _imp->internalColorspace;
}

void
KnobColor::setSimplified(bool simp)
{
    _imp->simplifiedMode = simp;
}

bool
KnobColor::isSimplified() const
{
    return _imp->simplifiedMode;
}

struct KnobStringPrivate
{

    bool multiLine;
    bool richText;
    bool customHtmlText;
    bool isLabel;
    bool isCustom;
    int fontSize;
    bool boldActivated;
    bool italicActivated;
    std::string fontFamily;
    double fontColor[3];

    KnobStringPrivate()
    : multiLine(false)
    , richText(false)
    , customHtmlText(false)
    , isLabel(false)
    , isCustom(false)
    , fontSize()
    , boldActivated(false)
    , italicActivated(false)
    , fontFamily(NATRON_FONT)
    , fontColor()
    {

    }
};

KnobString::KnobString(const KnobHolderPtr& holder,
                       const std::string &name,
                       int dimension)
: KnobStringBase(holder, name, dimension)
, _imp(new KnobStringPrivate())
{
    _imp->fontSize = getDefaultFontPointSize();
    _imp->fontColor[0] = _imp->fontColor[1] = _imp->fontColor[2] = 0.;
}

KnobString::KnobString(const KnobHolderPtr& holder, const KnobIPtr& mainInstance)
: KnobStringBase(holder, mainInstance)
, _imp(toKnobString(mainInstance)->_imp)
{

}

KnobString::~KnobString()
{
}

int
KnobString::getDefaultFontPointSize()
{
    return kKnobStringDefaultFontSize;
}

bool
KnobString::canAnimate() const
{
    return canAnimateStatic();
}

const std::string KnobString::_typeNameStr(kKnobStringTypeName);
const std::string &
KnobString::typeNameStatic()
{
    return _typeNameStr;
}

const std::string &
KnobString::typeName() const
{
    return typeNameStatic();
}

bool
KnobString::parseFont(const QString & label, int* fontSize, QString* fontFamily, bool* isBold, bool* isItalic, double* r, double *g, double* b)
{
    assert(isBold && isItalic && r && g && b && fontFamily && fontSize);

    *isBold = false;
    *isItalic = false;
    *fontSize = 0;
    *r = *g = *b = 0.;

    QString toFind = QString::fromUtf8(kFontSizeTag);
    int startFontTag = label.indexOf(toFind);

    assert(startFontTag != -1);
    if (startFontTag == -1) {
        return false;
    }
    startFontTag += toFind.size();
    int j = startFontTag;
    QString sizeStr;
    while ( j < label.size() && label.at(j).isDigit() ) {
        sizeStr.push_back( label.at(j) );
        ++j;
    }

    toFind = QString::fromUtf8(kFontFaceTag);
    startFontTag = label.indexOf(toFind, startFontTag);
    assert(startFontTag != -1);
    if (startFontTag == -1) {
        return false;
    }
    startFontTag += toFind.size();
    j = startFontTag;
    QString faceStr;
    while ( j < label.size() && label.at(j) != QLatin1Char('"') ) {
        faceStr.push_back( label.at(j) );
        ++j;
    }

    *fontSize = sizeStr.toInt();
    *fontFamily = faceStr;

    {
        toFind = QString::fromUtf8(kBoldStartTag);
        int foundBold = label.indexOf(toFind);
        if (foundBold != -1) {
            *isBold = true;
        }
    }

    {
        toFind = QString::fromUtf8(kItalicStartTag);
        int foundItalic = label.indexOf(toFind);
        if (foundItalic != -1) {
            *isItalic = true;
        }
    }
    {
        toFind = QString::fromUtf8(kFontColorTag);
        int foundColor = label.indexOf(toFind);
        if (foundColor != -1) {
            foundColor += toFind.size();
            QString currentColor;
            int j = foundColor;
            while ( j < label.size() && label.at(j) != QLatin1Char('"') ) {
                currentColor.push_back( label.at(j) );
                ++j;
            }
            int red, green, blue;
            ColorParser::parseColor(currentColor, &red, &green, &blue);
            *r = red / 255.0;
            *g = green / 255.0;
            *b = blue / 255.0;
        }
    }
    return true;
} // KnobString::parseFont

bool
KnobString::hasContentWithoutHtmlTags()
{
    std::string str = getValue();

    if ( str.empty() ) {
        return false;
    }

    //First remove content in the NATRON_CUSTOM_HTML tags
    const std::string customTagStart(NATRON_CUSTOM_HTML_TAG_START);
    const std::string customTagEnd(NATRON_CUSTOM_HTML_TAG_END);
    std::size_t foundNatronCustomDataTag = str.find(customTagStart, 0);
    if (foundNatronCustomDataTag != std::string::npos) {
        ///remove the current custom data
        int foundNatronEndTag = str.find(customTagEnd, foundNatronCustomDataTag);
        assert(foundNatronEndTag != (int)std::string::npos);

        foundNatronEndTag += customTagEnd.size();
        str.erase(foundNatronCustomDataTag, foundNatronEndTag - foundNatronCustomDataTag);
    }

    std::size_t foundOpen = str.find("<");
    if (foundOpen == std::string::npos) {
        return true;
    }
    while (foundOpen != std::string::npos) {
        std::size_t foundClose = str.find(">", foundOpen);
        if (foundClose == std::string::npos) {
            return true;
        }

        if ( foundClose + 1 < str.size() ) {
            if (str[foundClose + 1] == '<') {
                foundOpen = foundClose + 1;
            } else {
                return true;
            }
        } else {
            return false;
        }
    }

    return true;
}


QString
KnobString::removeNatronHtmlTag(QString text)
{
    // We also remove any custom data added by natron so the user doesn't see it
    int startCustomData = text.indexOf( QString::fromUtf8(NATRON_CUSTOM_HTML_TAG_START) );

    if (startCustomData != -1) {

        // Found start tag, now find end tag and remove what's in-between
        QString endTag( QString::fromUtf8(NATRON_CUSTOM_HTML_TAG_END) );
        int endCustomData = text.indexOf(endTag, startCustomData);
        assert(endCustomData != -1);
        if (endCustomData == -1) {
            return text;
        }
        endCustomData += endTag.size();
        text.remove(startCustomData, endCustomData - startCustomData);
    }

    return text;
}

QString
KnobString::getNatronHtmlTagContent(QString text)
{
    QString label = removeAutoAddedHtmlTags(text, false);
    QString startTag = QString::fromUtf8(NATRON_CUSTOM_HTML_TAG_START);
    int startCustomData = label.indexOf(startTag);

    if (startCustomData != -1) {

        // Found start tag, now find end tag and get what's in-between
        QString endTag = QString::fromUtf8(NATRON_CUSTOM_HTML_TAG_END);
        int endCustomData = label.indexOf(endTag, startCustomData);
        assert(endCustomData != -1);
        if (endCustomData == -1) {
            return label;
        }
        label = label.remove( endCustomData, endTag.size() );
        label = label.remove( startCustomData, startTag.size() );
    }

    return label;
}

QString
KnobString::removeAutoAddedHtmlTags(QString text,
                                       bool removeNatronTag)
{
    // Find font start tag
    QString toFind = QString::fromUtf8(kFontSizeTag);
    int i = text.indexOf(toFind);
    bool foundFontStart = i != -1;

    // Remove bold tag
    QString boldStr = QString::fromUtf8(kBoldStartTag);
    int foundBold = text.lastIndexOf(boldStr, i);

    // Assert removed: the knob might be linked from elsewhere and the button might not have been pressed.
    //assert((foundBold == -1 && !_boldActivated) || (foundBold != -1 && _boldActivated));

    if (foundBold != -1) {
        // We found bold, remove it
        text.remove( foundBold, boldStr.size() );
        boldStr = QString::fromUtf8(kBoldEndTag);
        foundBold = text.lastIndexOf(boldStr);
        assert(foundBold != -1);
        if (foundBold == -1) {
            return text;
        }
        text.remove( foundBold, boldStr.size() );
    }

    // Refresh the index of the font start tag
    i = text.indexOf(toFind);

    // Remove italic tag
    QString italStr = QString::fromUtf8(kItalicStartTag);
    int foundItal = text.lastIndexOf(italStr, i);

    // Assert removed: the knob might be linked from elsewhere and the button might not have been pressed.
    // assert((_italicActivated && foundItal != -1) || (!_italicActivated && foundItal == -1));

    if (foundItal != -1) {
        // We found italic, remove it
        text.remove( foundItal, italStr.size() );
        italStr = QString::fromUtf8(kItalicEndTag);
        foundItal = text.lastIndexOf(italStr);
        assert(foundItal != -1);
        text.remove( foundItal, italStr.size() );
    }

    // Refresh the index of the font start tag
    i = text.indexOf(toFind);

    // Find the end of the font declaration start tag
    QString endTag = QString::fromUtf8("\">");
    int foundEndTag = text.indexOf(endTag, i);
    foundEndTag += endTag.size();
    if (foundFontStart) {
        //Remove the whole font declaration tag
        text.remove(i, foundEndTag - i);
    }

    // Find the font end tag
    endTag = QString::fromUtf8(kFontEndTag);
    foundEndTag = text.lastIndexOf(endTag);
    assert( (foundEndTag != -1 && foundFontStart) || !foundFontStart );
    if (foundEndTag != -1) {
        // Remove the font end tag
        text.remove( foundEndTag, endTag.size() );
    }

    // We also remove any custom data added by natron so the user doesn't see it
    if (removeNatronTag) {
        return removeNatronHtmlTag(text);
    } else {
        return text;
    }
} // removeAutoAddedHtmlTags

QString
KnobString::makeFontTag(const QString& family,
                        int fontSize,
                        double r, double g, double b)
{
    QString colorName = ColorParser::getColorName(Image::clamp(r, 0., 1.) * 255.0, Image::clamp(g, 0., 1.) * 255.0, Image::clamp(b, 0., 1.) * 255.0);
    return QString::fromUtf8(kFontSizeTag "%1\" " kFontColorTag "%2\" " kFontFaceTag "%3\">")
    .arg(fontSize)
    .arg(colorName)
    .arg(family);
}

QString
KnobString::decorateTextWithFontTag(const QString& family,
                                    int fontSize,
                                    double r, double g, double b,
                                    bool isBold, bool isItalic,
                                    const QString& text)
{
    QString ret = makeFontTag(family, fontSize, r, g, b);
    if (isBold) {
        ret += QString::fromUtf8(kBoldStartTag);
    }
    if (isItalic) {
        ret += QString::fromUtf8(kItalicStartTag);
    }
    ret += text;
    if (isBold) {
        ret += QString::fromUtf8(kBoldEndTag);
    }
    if (isItalic) {
        ret += QString::fromUtf8(kItalicEndTag);
    }

    ret += QString::fromUtf8(kFontEndTag);
    return ret;
}

QString
KnobString::decorateStringWithCurrentState(const QString& str)
{
    QString ret = str;
    if (!_imp->richText) {
        return ret;
    }
    ret = decorateTextWithFontTag(QString::fromUtf8(_imp->fontFamily.c_str()), _imp->fontSize, _imp->fontColor[0], _imp->fontColor[1], _imp->fontColor[2], _imp->boldActivated, _imp->italicActivated, ret);
    return ret;
}

QString
KnobString::getValueDecorated(TimeValue time, ViewIdx view)
{
    QString ret;
    if (isAnimated(DimIdx(0), view)) {
        ret = QString::fromUtf8(getValueAtTime(time, DimIdx(0) , view).c_str());
    } else {
        ret = QString::fromUtf8(getValue(DimIdx(0), view).c_str());
    }
    return decorateStringWithCurrentState(ret);
}


void
KnobString::setAsMultiLine()
{
    _imp->multiLine = true;
}

void
KnobString::setUsesRichText(bool useRichText)
{
    _imp->richText = useRichText;
}

bool
KnobString::isMultiLine() const
{
    return _imp->multiLine;
}

bool
KnobString::usesRichText() const
{
    return _imp->richText;
}

void
KnobString::setAsCustomHTMLText(bool custom)
{
    _imp->customHtmlText = custom;
}

bool
KnobString::isCustomHTMLText() const
{
    return _imp->customHtmlText;
}


void
KnobString::setAsLabel()
{
    setAnimationEnabled(false); //< labels cannot animate
    _imp->isLabel = true;
}

bool
KnobString::isLabel() const
{
    return _imp->isLabel;
}

void
KnobString::setAsCustom()
{
    _imp->isCustom = true;
}

bool
KnobString::isCustomKnob() const
{
    return _imp->isCustom;
}

bool
KnobString::supportsInViewerContext() const
{
    return !_imp->multiLine;
}

int
KnobString::getFontSize() const
{
    return _imp->fontSize;
}

void
KnobString::setFontSize(int size)
{
    _imp->fontSize = size;
}

std::string
KnobString::getFontFamily() const
{
    return _imp->fontFamily;
}

void
KnobString::setFontFamily(const std::string& family) {
    _imp->fontFamily = family;
}

void
KnobString::getFontColor(double* r, double* g, double* b) const
{
    *r = _imp->fontColor[0];
    *g = _imp->fontColor[1];
    *b = _imp->fontColor[2];
}

void
KnobString::setFontColor(double r, double g, double b)
{
    _imp->fontColor[0] = r;
    _imp->fontColor[1] = g;
    _imp->fontColor[2] = b;
}

bool
KnobString::getItalicActivated() const
{
    return _imp->italicActivated;
}

void
KnobString::setItalicActivated(bool b) {
    _imp->italicActivated = b;
}

bool
KnobString::getBoldActivated() const
{
    return _imp->boldActivated;
}

void
KnobString::setBoldActivated(bool b) {
    _imp->boldActivated = b;
}


/******************************KnobGroup**************************************/

struct KnobGroupPrivate
{
    std::vector< KnobIWPtr > children;
    bool isTab;
    bool isToolButton;
    bool isDialog;

    KnobGroupPrivate()
    : isTab(false)
    , isToolButton(false)
    , isDialog(false)
    {

    }

};
KnobGroup::KnobGroup(const KnobHolderPtr& holder,
                     const std::string &name,
                     int dimension)
: KnobBoolBase(holder, name, dimension)
, _imp(new KnobGroupPrivate)
{
}

KnobGroup::KnobGroup(const KnobHolderPtr& holder, const KnobIPtr& mainInstance)
: KnobBoolBase(holder, mainInstance)
, _imp(toKnobGroup(mainInstance)->_imp)
{

}

KnobGroup::~KnobGroup()
{

}

void
KnobGroup::setAsTab()
{
    _imp->isTab = true;
}

bool
KnobGroup::isTab() const
{
    return _imp->isTab;
}

void
KnobGroup::setAsToolButton(bool b)
{
    _imp->isToolButton = b;
}

bool
KnobGroup::getIsToolButton() const
{
    return _imp->isToolButton;
}

void
KnobGroup::setAsDialog(bool b)
{
    _imp->isDialog = b;
}

bool
KnobGroup::getIsDialog() const
{
    return _imp->isDialog;
}

bool
KnobGroup::canAnimate() const
{
    return false;
}

const std::string KnobGroup::_typeNameStr(kKnobGroupTypeName);
const std::string &
KnobGroup::typeNameStatic()
{
    return _typeNameStr;
}

const std::string &
KnobGroup::typeName() const
{
    return typeNameStatic();
}

void
KnobGroup::addKnob(const KnobIPtr& k)
{
    if ( !isUserKnob() && k->isUserKnob() ) {
        return;
    }

    for (std::size_t i = 0; i < _imp->children.size(); ++i) {
        if (_imp->children[i].lock() == k) {
            return;
        }
    }

    k->resetParent();

    _imp->children.push_back(k);
    k->setParentKnob( shared_from_this() );
}

void
KnobGroup::removeKnob(const KnobIPtr& k)
{
    for (std::vector<KnobIWPtr >::iterator it = _imp->children.begin(); it != _imp->children.end(); ++it) {
        if (it->lock() == k) {
            _imp->children.erase(it);

            return;
        }
    }
}

bool
KnobGroup::moveOneStepUp(const KnobIPtr& k)
{
    for (U32 i = 0; i < _imp->children.size(); ++i) {
        if (_imp->children[i].lock() == k) {
            if (i == 0) {
                return false;
            }
            KnobIWPtr tmp = _imp->children[i - 1];
            _imp->children[i - 1] = _imp->children[i];
            _imp->children[i] = tmp;

            return true;
        }
    }
    throw std::invalid_argument("Given knob does not belong to this group");
}

bool
KnobGroup::moveOneStepDown(const KnobIPtr& k)
{
    for (U32 i = 0; i < _imp->children.size(); ++i) {
        if (_imp->children[i].lock() == k) {
            if (i == _imp->children.size() - 1) {
                return false;
            }
            KnobIWPtr tmp = _imp->children[i + 1];
            _imp->children[i + 1] = _imp->children[i];
            _imp->children[i] = tmp;

            return true;
        }
    }
    throw std::invalid_argument("Given knob does not belong to this group");
}

void
KnobGroup::insertKnob(int index,
                      const KnobIPtr& k)
{
    if ( !isUserKnob() && k->isUserKnob() ) {
        return;
    }

    for (std::size_t i = 0; i < _imp->children.size(); ++i) {
        if (_imp->children[i].lock() == k) {
            return;
        }
    }

    k->resetParent();

    if ( index >= (int)_imp->children.size() ) {
        _imp->children.push_back(k);
    } else {
        std::vector<KnobIWPtr >::iterator it = _imp->children.begin();
        std::advance(it, index);
        _imp->children.insert(it, k);
    }
    k->setParentKnob( shared_from_this() );
}

std::vector< KnobIPtr >
KnobGroup::getChildren() const
{
    std::vector< KnobIPtr > ret;

    for (std::size_t i = 0; i < _imp->children.size(); ++i) {
        KnobIPtr k = _imp->children[i].lock();
        if (k) {
            ret.push_back(k);
        }
    }

    return ret;
}

/******************************PAGE_KNOB**************************************/
struct KnobPagePrivate
{
    bool isToolBar;
    std::vector< KnobIWPtr > children;

    KnobPagePrivate()
    : isToolBar(false)
    , children()
    {

    }
};

KnobPage::KnobPage(const KnobHolderPtr& holder,
                   const std::string &name,
                   int dimension)
: KnobBoolBase(holder, name, dimension)
, _imp(new KnobPagePrivate())
{
    setIsPersistent(false);
}

KnobPage::KnobPage(const KnobHolderPtr& holder, const KnobIPtr& mainInstance)
: KnobBoolBase(holder, mainInstance)
, _imp(toKnobPage(mainInstance)->_imp)
{

}

KnobPage::~KnobPage()
{

}

void
KnobPage::setAsToolBar(bool b)
{
    _imp->isToolBar = b;
}

bool
KnobPage::getIsToolBar() const
{
    return _imp->isToolBar;
}


bool
KnobPage::canAnimate() const
{
    return false;
}

const std::string KnobPage::_typeNameStr(kKnobPageTypeName);
const std::string &
KnobPage::typeNameStatic()
{
    return _typeNameStr;
}

const std::string &
KnobPage::typeName() const
{
    return typeNameStatic();
}

std::vector< KnobIPtr >
KnobPage::getChildren() const
{
    std::vector< KnobIPtr > ret;

    for (std::size_t i = 0; i < _imp->children.size(); ++i) {
        KnobIPtr k = _imp->children[i].lock();
        if (k) {
            ret.push_back(k);
        }
    }

    return ret;
}

void
KnobPage::addKnob(const KnobIPtr &k)
{
    if ( !isUserKnob() && k->isUserKnob() ) {
        return;
    }
    for (std::size_t i = 0; i < _imp->children.size(); ++i) {
        if (_imp->children[i].lock() == k) {
            return;
        }
    }


    k->resetParent();

    _imp->children.push_back(k);
    k->setParentKnob( shared_from_this() );
}

void
KnobPage::insertKnob(int index,
                     const KnobIPtr& k)
{
    if ( !isUserKnob() && k->isUserKnob() ) {
        return;
    }

    for (std::size_t i = 0; i < _imp->children.size(); ++i) {
        if (_imp->children[i].lock() == k) {
            return;
        }
    }

    k->resetParent();

    if ( index >= (int)_imp->children.size() ) {
        _imp->children.push_back(k);
    } else {
        std::vector<KnobIWPtr >::iterator it = _imp->children.begin();
        std::advance(it, index);
        _imp->children.insert(it, k);
    }
    k->setParentKnob( shared_from_this() );
}

void
KnobPage::removeKnob(const KnobIPtr& k)
{
    for (std::vector<KnobIWPtr >::iterator it = _imp->children.begin(); it != _imp->children.end(); ++it) {
        if (it->lock() == k) {
            _imp->children.erase(it);

            return;
        }
    }
}

bool
KnobPage::moveOneStepUp(const KnobIPtr& k)
{
    for (U32 i = 0; i < _imp->children.size(); ++i) {
        if (_imp->children[i].lock() == k) {
            if (i == 0) {
                return false;
            }
            KnobIWPtr tmp = _imp->children[i - 1];
            _imp->children[i - 1] = _imp->children[i];
            _imp->children[i] = tmp;

            return true;
        }
    }
    throw std::invalid_argument("Given knob does not belong to this page");
}

bool
KnobPage::moveOneStepDown(const KnobIPtr& k)
{
    for (U32 i = 0; i < _imp->children.size(); ++i) {
        if (_imp->children[i].lock() == k) {
            if (i == _imp->children.size() - 1) {
                return false;
            }
            KnobIWPtr tmp = _imp->children[i + 1];
            _imp->children[i + 1] = _imp->children[i];
            _imp->children[i] = tmp;

            return true;
        }
    }
    throw std::invalid_argument("Given knob does not belong to this page");
}

/******************************KnobParametric**************************************/

bool
ParametricKnobDimView::copy(const CopyInArgs& inArgs, CopyOutArgs* outArgs)
{
    bool hasChanged = ValueKnobDimView<double>::copy(inArgs, outArgs);
    const ParametricKnobDimView* otherType = dynamic_cast<const ParametricKnobDimView*>(inArgs.other);
    assert(otherType);

    QMutexLocker k(&valueMutex);
    QMutexLocker k2(&inArgs.other->valueMutex);

    if (otherType->parametricCurve) {
        if (!parametricCurve) {
            parametricCurve.reset(new Curve(otherType->parametricCurve->getType()));
        }
        hasChanged |= parametricCurve->cloneAndCheckIfChanged(*otherType->parametricCurve, 0 /*offset*/, 0 /*range*/);
    }
    return hasChanged;
}

struct KnobParametricSharedData
{
    mutable QMutex curvesMutex;
    std::vector< CurvePtr >  defaultCurves;
    std::vector<RGBAColourD> curvesColor;


    KnobParametricSharedData(int dimension)
    : curvesMutex()
    , defaultCurves(dimension)
    , curvesColor(dimension)
    {

    }

};

// Render local curves
struct KnobParametricRenderCurves
{
    std::vector<CurvePtr> curves;
};

struct KnobParametricPrivate
{
    boost::shared_ptr<KnobParametricSharedData> common;

    boost::scoped_ptr<KnobParametricRenderCurves> renderLocalCurves;

    KnobParametricPrivate()
    : common()
    , renderLocalCurves()
    {

    }
};

KnobParametric::KnobParametric(const KnobHolderPtr& holder,
                               const std::string &name,
                               int dimension)
: KnobDoubleBase(holder, name, dimension)
, _imp(new KnobParametricPrivate())
{
    _imp->common.reset(new KnobParametricSharedData(dimension));

    setCanAutoFoldDimensions(false);
}

KnobParametric::KnobParametric(const KnobHolderPtr& holder, const KnobIPtr& mainInstance)
: KnobDoubleBase(holder, mainInstance)
, _imp(new KnobParametricPrivate())
{
    _imp->renderLocalCurves.reset(new KnobParametricRenderCurves);
    _imp->renderLocalCurves->curves.resize(getNDimensions());
    _imp->common = toKnobParametric(mainInstance)->_imp->common;
}

KnobParametric::~KnobParametric()
{
    
}

KnobDimViewBasePtr
KnobParametric::createDimViewData() const
{
    ParametricKnobDimViewPtr ret(new ParametricKnobDimView);
    ret->parametricCurve.reset(new Curve(eCurveTypeDouble));
    ret->parametricCurve->setKeyFramesTimeClampedToIntegers(false);
    return ret;
}

void
KnobParametric::populate()
{
    KnobDoubleBase::populate();
    for (int i = 0; i < getNDimensions(); ++i) {
        RGBAColourD color;
        color.r = color.g = color.b = color.a = 1.;
        _imp->common->curvesColor[i] = color;
        _imp->common->defaultCurves[i].reset(new Curve(eCurveTypeDouble));
        _imp->common->defaultCurves[i]->setKeyFramesTimeClampedToIntegers(false);
    }
}

const std::string KnobParametric::_typeNameStr(kKnobParametricTypeName);

void
KnobParametric::setPeriodic(bool periodic)
{
    for (std::size_t i = 0; i < _imp->common->defaultCurves.size(); ++i) {
        ParametricKnobDimViewPtr data = toParametricKnobDimView(getDataForDimView(DimIdx(i), ViewIdx(0)));
        assert(data);
        data->parametricCurve->setPeriodic(periodic);
        _imp->common->defaultCurves[i]->setPeriodic(periodic);
    }
}


const std::string &
KnobParametric::typeNameStatic()
{
    return _typeNameStr;
}


bool
KnobParametric::canAnimate() const
{
    return false;
}

const std::string &
KnobParametric::typeName() const
{
    return typeNameStatic();
}

CurvePtr
KnobParametric::getAnimationCurve(ViewIdx idx, DimIdx dimension) const
{
    if (dimension < 0 || dimension >= (int)_imp->common->defaultCurves.size()) {
        throw std::invalid_argument("KnobParametric::getAnimationCurve dimension out of range");
    }
    ViewIdx view_i = checkIfViewExistsOrFallbackMainView(idx);
    ParametricKnobDimViewPtr data = toParametricKnobDimView(getDataForDimView(dimension, view_i));
    if (!data) {
        return CurvePtr();
    }
    return data->parametricCurve;
}

void
KnobParametric::setCurveColor(DimIdx dimension,
                              double r,
                              double g,
                              double b)
{
    ///only called in the main thread
    assert( QThread::currentThread() == qApp->thread() );
    ///Mt-safe as it never changes

    assert( dimension < (int)_imp->common->curvesColor.size() );
    _imp->common->curvesColor[dimension].r = r;
    _imp->common->curvesColor[dimension].g = g;
    _imp->common->curvesColor[dimension].b = b;

    Q_EMIT curveColorChanged(dimension);
}

void
KnobParametric::getCurveColor(DimIdx dimension,
                              double* r,
                              double* g,
                              double* b)
{
    ///Mt-safe as it never changes

    if (dimension < 0 || dimension >= (int)_imp->common->defaultCurves.size()) {
        throw std::invalid_argument("KnobParametric::getCurveColor dimension out of range");
    }

    *r = _imp->common->curvesColor[dimension].r;
    *g = _imp->common->curvesColor[dimension].g;
    *b = _imp->common->curvesColor[dimension].b;

}

void
KnobParametric::setParametricRange(double min,
                                   double max)
{
    ///only called in the main thread
    assert( QThread::currentThread() == qApp->thread() );
    ///Mt-safe as it never changes

    for (std::size_t i = 0; i < _imp->common->defaultCurves.size(); ++i) {
        ParametricKnobDimViewPtr data = toParametricKnobDimView(getDataForDimView(DimIdx(i), ViewIdx(0)));
        assert(data);
        data->parametricCurve->setXRange(min, max);
    }
}

std::pair<double, double> KnobParametric::getParametricRange() const
{
    ///Mt-safe as it never changes
    ParametricKnobDimViewPtr data = toParametricKnobDimView(getDataForDimView(DimIdx(0), ViewIdx(0)));
    assert(data);
    return data->parametricCurve->getXRange();
}

CurvePtr
KnobParametric::getDefaultParametricCurve(DimIdx dimension) const
{
    if (dimension < 0 || dimension >= (int)_imp->common->defaultCurves.size()) {
        throw std::invalid_argument("KnobParametric::getDefaultParametricCurve dimension out of range");
    }
    return _imp->common->defaultCurves[dimension];

}

void
KnobParametric::clearRenderValuesCache()
{
    if (_imp->renderLocalCurves) {
        _imp->renderLocalCurves->curves.clear();
        _imp->renderLocalCurves->curves.resize(getNDimensions());
    }
}

CurvePtr
KnobParametric::getParametricCurveInternal(DimIdx dimension, ViewIdx view, ParametricKnobDimViewPtr* outData) const
{
    ///Mt-safe as Curve is MT-safe and the pointer is never deleted
    if (dimension < 0 || dimension >= (int)_imp->common->defaultCurves.size()) {
        throw std::invalid_argument("KnobParametric::getParametricCurve dimension out of range");
    }
    ViewIdx view_i = checkIfViewExistsOrFallbackMainView(view);
    ParametricKnobDimViewPtr data = toParametricKnobDimView(getDataForDimView(dimension, view_i));
    if (!data) {
        return CurvePtr();
    }
    if (outData) {
        *outData = data;
    }

    EffectInstancePtr holder = toEffectInstance(getHolder());
    if (holder) {
        if (_imp->renderLocalCurves) {
            if (_imp->renderLocalCurves->curves[dimension]) {
                return _imp->renderLocalCurves->curves[dimension];
            }
            CurvePtr clone(new Curve());
            clone->clone(*data->parametricCurve);
            _imp->renderLocalCurves->curves[dimension] = clone;
            return clone;
        }
    }
    return data->parametricCurve;
}

CurvePtr KnobParametric::getParametricCurve(DimIdx dimension, ViewIdx view) const
{
    return getParametricCurveInternal(dimension, view, 0);
}

void
KnobParametric::signalCurveChanged(DimSpec dimension, const KnobDimViewBasePtr& data)
{
    KnobDimViewKeySet sharedKnobs;
    {
        QMutexLocker k(&data->valueMutex);
        sharedKnobs = data->sharedKnobs;
    }
    for (KnobDimViewKeySet::const_iterator it = sharedKnobs.begin(); it!=sharedKnobs.end(); ++it) {
        KnobParametricPtr sharedKnob = toKnobParametric(it->knob.lock());
        assert(sharedKnob);
        if (!sharedKnob) {
            continue;
        }

        Q_EMIT sharedKnob->curveChanged(dimension);
    }
}

ActionRetCodeEnum
KnobParametric::addControlPoint(ValueChangedReasonEnum reason,
                                DimIdx dimension,
                                double key,
                                double value,
                                KeyframeTypeEnum interpolation)
{
    ///Mt-safe as Curve is MT-safe
    if ( ( dimension >= (int)_imp->common->defaultCurves.size() ) ||
         ( key != key) || // check for NaN
         boost::math::isinf(key) ||
         ( value != value) || // check for NaN
         boost::math::isinf(value) ) {
        return eActionStatusFailed;
    }

    KeyFrame k(key, value);
    k.setInterpolation(interpolation);

    ParametricKnobDimViewPtr data;
    CurvePtr curve = getParametricCurveInternal(dimension, ViewIdx(0), &data);
    assert(curve);
    curve->setOrAddKeyframe(k);
    evaluateValueChange(DimIdx(0), getCurrentRenderTime(), ViewSetSpec::all(), reason);
    signalCurveChanged(dimension, data);
    return eActionStatusOK;
}

ActionRetCodeEnum
KnobParametric::addControlPoint(ValueChangedReasonEnum reason,
                                DimIdx dimension,
                                double key,
                                double value,
                                double leftDerivative,
                                double rightDerivative,
                                KeyframeTypeEnum interpolation)
{
    ///Mt-safe as Curve is MT-safe
    if ( ( dimension >= (int)_imp->common->defaultCurves.size() ) ||
         ( key != key) || // check for NaN
         boost::math::isinf(key) ||
         ( value != value) || // check for NaN
         boost::math::isinf(value) ) {
        return eActionStatusFailed;
    }

    KeyFrame k(key, value, leftDerivative, rightDerivative);
    k.setInterpolation(interpolation);
    ParametricKnobDimViewPtr data;
    CurvePtr curve = getParametricCurveInternal(dimension, ViewIdx(0), &data);
    assert(curve);
    curve->setOrAddKeyframe(k);
    signalCurveChanged(dimension, data);
    evaluateValueChange(DimIdx(0), getCurrentRenderTime(), ViewSetSpec::all(), reason);

    return eActionStatusOK;
}

ActionRetCodeEnum
KnobParametric::evaluateCurve(DimIdx dimension,
                         ViewIdx view,
                         double parametricPosition,
                         double *returnValue) const
{
    ///Mt-safe as Curve is MT-safe
    if ( dimension >= (int)_imp->common->defaultCurves.size() ) {
        return eActionStatusFailed;
    }
    CurvePtr curve = getParametricCurve(dimension, view);
    if (!curve) {
        return eActionStatusFailed;
    }
    *returnValue = curve->getValueAt(TimeValue(parametricPosition)).getValue();
    return eActionStatusOK;
}

ActionRetCodeEnum
KnobParametric::getNControlPoints(DimIdx dimension,
                                  ViewIdx view,
                                  int *returnValue) const
{
    ///Mt-safe as Curve is MT-safe
    if ( dimension >= (int)_imp->common->defaultCurves.size() ) {
        return eActionStatusFailed;
    }
    CurvePtr curve = getParametricCurve(dimension, view);
    if (!curve) {
        return eActionStatusFailed;
    }
    *returnValue =  curve->getKeyFramesCount();

    return eActionStatusOK;
}

ActionRetCodeEnum
KnobParametric::getNthControlPoint(DimIdx dimension,
                                   ViewIdx view,
                                   int nthCtl,
                                   double *key,
                                   double *value) const
{
    ///Mt-safe as Curve is MT-safe
    if ( dimension >= (int)_imp->common->defaultCurves.size() ) {
        return eActionStatusFailed;
    }
    CurvePtr curve = getParametricCurve(dimension, view);
    if (!curve) {
        return eActionStatusFailed;
    }

    KeyFrame kf;
    bool ret = curve->getKeyFrameWithIndex(nthCtl, &kf);
    if (!ret) {
        return eActionStatusFailed;
    }
    *key = kf.getTime();
    *value = kf.getValue();

    return eActionStatusOK;
}

ActionRetCodeEnum
KnobParametric::getNthControlPoint(DimIdx dimension,
                                   ViewIdx view,
                                   int nthCtl,
                                   double *key,
                                   double *value,
                                   double *leftDerivative,
                                   double *rightDerivative) const
{
    ///Mt-safe as Curve is MT-safe
    if ( dimension >= (int)_imp->common->defaultCurves.size() ) {
        return eActionStatusFailed;
    }
    CurvePtr curve = getParametricCurve(dimension, view);
    if (!curve) {
        return eActionStatusFailed;
    }
    KeyFrame kf;
    bool ret = curve->getKeyFrameWithIndex(nthCtl, &kf);
    if (!ret) {
        return eActionStatusFailed;
    }
    *key = kf.getTime();
    *value = kf.getValue();
    *leftDerivative = kf.getLeftDerivative();
    *rightDerivative = kf.getRightDerivative();

    return eActionStatusOK;
}

ActionRetCodeEnum
KnobParametric::setNthControlPointInterpolation(ValueChangedReasonEnum reason,
                                                DimIdx dimension,
                                                ViewSetSpec view,
                                                int nThCtl,
                                                KeyframeTypeEnum interpolation)
{

    ///Mt-safe as Curve is MT-safe
    if ( dimension >= (int)_imp->common->defaultCurves.size() ) {
        return eActionStatusFailed;
    }
    std::list<ViewIdx> views = getViewsList();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        ParametricKnobDimViewPtr data;
        CurvePtr curve = getParametricCurveInternal(dimension, *it, &data);
        if (!curve) {
            return eActionStatusFailed;
        }

        try {
            curve->setKeyFrameInterpolation(interpolation, nThCtl);
        } catch (...) {
            return eActionStatusFailed;
        }
        signalCurveChanged(dimension, data);

    }



    evaluateValueChange(dimension, getCurrentRenderTime(), view, reason);

    return eActionStatusOK;
}

ActionRetCodeEnum
KnobParametric::setNthControlPoint(ValueChangedReasonEnum reason,
                                   DimIdx dimension,
                                   ViewSetSpec view,
                                   int nthCtl,
                                   double key,
                                   double value)
{
    ///Mt-safe as Curve is MT-safe
    if ( dimension >= (int)_imp->common->defaultCurves.size() ) {
        return eActionStatusFailed;
    }
    std::list<ViewIdx> views = getViewsList();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        ParametricKnobDimViewPtr data;
        CurvePtr curve = getParametricCurveInternal(dimension, *it, &data);
        if (!curve) {
            return eActionStatusFailed;
        }
        try {
            curve->setKeyFrameValueAndTime(TimeValue(key), value, nthCtl);
        } catch (...) {
            return eActionStatusFailed;
        }
        signalCurveChanged(dimension, data);
    }


    evaluateValueChange(dimension, getCurrentRenderTime(), view, reason);

    return eActionStatusOK;
}

ActionRetCodeEnum
KnobParametric::setNthControlPoint(ValueChangedReasonEnum reason,
                                   DimIdx dimension,
                                   ViewSetSpec view,
                                   int nthCtl,
                                   double key,
                                   double value,
                                   double leftDerivative,
                                   double rightDerivative)
{
    if ( dimension >= (int)_imp->common->defaultCurves.size() ) {
        return eActionStatusFailed;
    }
    std::list<ViewIdx> views = getViewsList();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        ParametricKnobDimViewPtr data;
        CurvePtr curve = getParametricCurveInternal(dimension, *it, &data);
        if (!curve) {
            return eActionStatusFailed;
        }
        int newIdx;
        try {
            curve->setKeyFrameValueAndTime(TimeValue(key), value, nthCtl, &newIdx);
        } catch (...) {
            return eActionStatusFailed;
        }
        curve->setKeyFrameDerivatives(leftDerivative, rightDerivative, newIdx);
        signalCurveChanged(dimension, data);
    }

    evaluateValueChange(dimension, getCurrentRenderTime(), view, reason);

    return eActionStatusOK;
} // setNthControlPoint

ActionRetCodeEnum
KnobParametric::deleteControlPoint(ValueChangedReasonEnum reason,
                                   DimIdx dimension,
                                   ViewSetSpec view,
                                   int nthCtl)
{
    if ( dimension >= (int)_imp->common->defaultCurves.size() ) {
        return eActionStatusFailed;
    }
    std::list<ViewIdx> views = getViewsList();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        ParametricKnobDimViewPtr data;
        CurvePtr curve = getParametricCurveInternal(dimension, *it, &data);
        if (!curve) {
            return eActionStatusFailed;
        }
        try {
            curve->removeKeyFrameWithIndex(nthCtl);
        } catch (...) {
            return eActionStatusFailed;
        }
        signalCurveChanged(dimension, data);
    }

    evaluateValueChange(dimension, getCurrentRenderTime(), view, reason);

    return eActionStatusOK;
}

ActionRetCodeEnum
KnobParametric::deleteAllControlPoints(ValueChangedReasonEnum reason,
                                       DimIdx dimension,
                                       ViewSetSpec view)
{
    if ( dimension >= (int)_imp->common->defaultCurves.size() ) {
        return eActionStatusFailed;
    }
    std::list<ViewIdx> views = getViewsList();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        ParametricKnobDimViewPtr data;
        CurvePtr curve = getParametricCurveInternal(dimension, *it, &data);
        if (!curve) {
            return eActionStatusFailed;
        }
        curve->clearKeyFrames();
        signalCurveChanged(dimension, data);

    }

    evaluateValueChange(DimIdx(0), getCurrentRenderTime(), ViewSetSpec::all(), reason);

    return eActionStatusOK;
}


void
KnobParametric::saveParametricCurves(std::map<std::string,std::list< SERIALIZATION_NAMESPACE::CurveSerialization > >* curves) const
{
    AppInstancePtr app = getHolder()->getApp();
    assert(app);
    std::vector<std::string> projectViews = app->getProject()->getProjectViewNames();
    std::list<ViewIdx> views = getViewsList();
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        std::string viewName;
        if (*it >= 0 && *it < (int)projectViews.size()) {
            viewName = projectViews[*it];
        }
        std::list< SERIALIZATION_NAMESPACE::CurveSerialization >& curveList = (*curves)[viewName];
        for (int i = 0; i < getNDimensions(); ++i) {
            CurvePtr curve = getParametricCurve(DimIdx(i), *it);
            assert(curve);
            SERIALIZATION_NAMESPACE::CurveSerialization c;
            if (curve) {
                curve->toSerialization(&c);
            }
            curveList.push_back(c);
        }
    }
} // saveParametricCurves

void
KnobParametric::loadParametricCurves(const std::map<std::string,std::list< SERIALIZATION_NAMESPACE::CurveSerialization > > & curves)
{
    AppInstancePtr app = getHolder()->getApp();
    assert(app);
    std::vector<std::string> projectViews = app->getProject()->getProjectViewNames();

    for (std::map<std::string,std::list< SERIALIZATION_NAMESPACE::CurveSerialization > >::const_iterator viewsIt = curves.begin(); viewsIt != curves.end(); ++viewsIt) {

        ViewIdx view_i(0);
        Project::getViewIndex(projectViews, viewsIt->first, &view_i);

        int i = 0;
        for (std::list< SERIALIZATION_NAMESPACE::CurveSerialization >::const_iterator it2 = viewsIt->second.begin(); it2 != viewsIt->second.end(); ++it2, ++i) {
            CurvePtr curve = getParametricCurve(DimIdx(i), view_i);
            if (!curve) {
                continue;
            }
            curve->fromSerialization(*it2);
        }
    }
} // loadParametricCurves

void
KnobParametric::resetExtraToDefaultValue(DimSpec dimension, ViewSetSpec view)
{

    std::list<ViewIdx> views = getViewsList();
    int nDims = getNDimensions();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (int i = 0; i < nDims; ++i) {
            if (!dimension.isAll() && dimension != i) {
                continue;
            }

            ParametricKnobDimViewPtr data;
            CurvePtr curve = getParametricCurveInternal(DimIdx(i), *it, &data);
            if (!curve) {
                continue;
            }

            curve->clone(*_imp->common->defaultCurves[DimIdx(i)]);
            signalCurveChanged(dimension, data);

        }

    }
}

void
KnobParametric::setDefaultCurvesFromCurves()
{
    for (std::size_t i = 0; i < _imp->common->defaultCurves.size(); ++i) {
        CurvePtr curve = getParametricCurve(DimIdx(i), ViewIdx(0));
        assert(curve);
        _imp->common->defaultCurves[i]->clone(*curve);
    }
    computeHasModifications();
}

bool
KnobParametric::hasModificationsVirtual(const KnobDimViewBasePtr& data, DimIdx dimension) const
{

    KeyFrameSet defKeys = _imp->common->defaultCurves[dimension]->getKeyFrames_mt_safe();
    ParametricKnobDimViewPtr parametricData = toParametricKnobDimView(data);
    assert(parametricData);
    assert(parametricData->parametricCurve);

    KeyFrameSet keys = parametricData->parametricCurve->getKeyFrames_mt_safe();
    if ( defKeys.size() != keys.size() ) {
        return true;
    }
    KeyFrameSet::iterator itO = defKeys.begin();
    for (KeyFrameSet::iterator it = keys.begin(); it != keys.end(); ++it, ++itO) {
        if (*it != *itO) {
            return true;
        }
    }

    return false;
}


void
KnobParametric::appendToHash(const ComputeHashArgs& args, Hash64* hash)
{

    if (args.hashType != HashableObject::eComputeHashTypeTimeViewVariant) {
        return;
    }
    for (std::size_t i = 0; i < _imp->common->defaultCurves.size(); ++i) {
        // Parametric params are a corner case:
        // The plug-in will try to call getValue at many parametric times, which are unknown.
        // The only way to identify uniquely the curve as a hash is to append all control points
        // of the curve to the hash.
        CurvePtr curve = getParametricCurve(DimIdx(i), args.view);
        if (curve) {
            Hash64::appendCurve(curve, hash);
        }

    }

}

bool
KnobParametric::cloneCurve(ViewIdx view, DimIdx dimension, const Curve& curve, double offset, const RangeD* range)
{
    if (dimension < 0 || dimension >= (int)_imp->common->defaultCurves.size()) {
        throw std::invalid_argument("KnobParametric: dimension out of range");
    }
    ParametricKnobDimViewPtr data;
    CurvePtr thisCurve = getParametricCurveInternal(dimension, view, &data);
    if (!thisCurve) {
        return false;
    }

    bool ret = thisCurve->cloneAndCheckIfChanged(curve, offset, range);
    if (ret) {
        signalCurveChanged(dimension, data);
        evaluateValueChange(dimension, getCurrentRenderTime(), ViewSetSpec(view), eValueChangedReasonUserEdited);
    }
    return ret;
}

void
KnobParametric::deleteValuesAtTime(const std::list<double>& times, ViewSetSpec view, DimSpec dimension, ValueChangedReasonEnum reason)
{
    std::list<ViewIdx> views = getViewsList();
    int nDims = getNDimensions();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (int i = 0; i < nDims; ++i) {
            if (!dimension.isAll() && dimension != i) {
                continue;
            }

            ParametricKnobDimViewPtr data;
            CurvePtr curve = getParametricCurveInternal(DimIdx(i), *it, &data);
            if (!curve) {
                continue;
            }

            for (std::list<double>::const_iterator it2 = times.begin(); it2!=times.end(); ++it2) {
                curve->removeKeyFrameWithTime(TimeValue(*it2));
            }
            signalCurveChanged(dimension, data);

        }
        
    }


    evaluateValueChange(dimension, getCurrentRenderTime(), view, reason);
}

bool
KnobParametric::warpValuesAtTime(const std::list<double>& times, ViewSetSpec view,  DimSpec dimension, const Curve::KeyFrameWarp& warp, std::vector<KeyFrame>* keyframes)
{
    bool ok = false;

    std::list<ViewIdx> views = getViewsList();
    int nDims = getNDimensions();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (int i = 0; i < nDims; ++i) {
            if (!dimension.isAll() && dimension != i) {
                continue;
            }

            ParametricKnobDimViewPtr data;
            CurvePtr curve = getParametricCurveInternal(DimIdx(i), *it, &data);
            if (!curve) {
                continue;
            }

            ok |= curve->transformKeyframesValueAndTime(times, warp, keyframes);
            signalCurveChanged(dimension, data);
        }

    }


    if (ok) {
        evaluateValueChange(dimension, getCurrentRenderTime(), view, eValueChangedReasonUserEdited);
        return true;
    }
    return false;
}

void
KnobParametric::removeAnimation(ViewSetSpec view, DimSpec dim, ValueChangedReasonEnum reason)
{
    std::list<ViewIdx> views = getViewsList();
    int nDims = getNDimensions();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (int i = 0; i < nDims; ++i) {
            if (!dim.isAll() && dim != i) {
                continue;
            }

            ParametricKnobDimViewPtr data;
            CurvePtr curve = getParametricCurveInternal(DimIdx(i), *it, &data);
            if (!curve) {
                continue;
            }

            curve->clearKeyFrames();
            signalCurveChanged(dim, data);
        }
        
    }


    evaluateValueChange(dim, getCurrentRenderTime(), view, reason);

}

void
KnobParametric::deleteAnimationBeforeTime(TimeValue time, ViewSetSpec view, DimSpec dimension)
{
    std::list<ViewIdx> views = getViewsList();
    int nDims = getNDimensions();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (int i = 0; i < nDims; ++i) {
            if (!dimension.isAll() && dimension != i) {
                continue;
            }

            ParametricKnobDimViewPtr data;
            CurvePtr curve = getParametricCurveInternal(DimIdx(i), *it, &data);
            if (!curve) {
                continue;
            }

            curve->removeKeyFramesAfterTime(time, 0);
            signalCurveChanged(dimension, data);
        }
        
    }

    evaluateValueChange(dimension, getCurrentRenderTime(), view, eValueChangedReasonUserEdited);

}

void
KnobParametric::deleteAnimationAfterTime(TimeValue time, ViewSetSpec view, DimSpec dimension)
{
    std::list<ViewIdx> views = getViewsList();
    int nDims = getNDimensions();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (int i = 0; i < nDims; ++i) {
            if (!dimension.isAll() && dimension != i) {
                continue;
            }

            ParametricKnobDimViewPtr data;
            CurvePtr curve = getParametricCurveInternal(DimIdx(i), *it, &data);
            if (!curve) {
                continue;
            }
            curve->removeKeyFramesAfterTime(time, 0);
            signalCurveChanged(dimension, data);
        }
        
    }


    evaluateValueChange(dimension, getCurrentRenderTime(), view, eValueChangedReasonUserEdited);
}

void
KnobParametric::setInterpolationAtTimes(ViewSetSpec view, DimSpec dimension, const std::list<double>& times, KeyframeTypeEnum interpolation, std::vector<KeyFrame>* newKeys)
{

    std::list<ViewIdx> views = getViewsList();
    int nDims = getNDimensions();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (int i = 0; i < nDims; ++i) {
            if (!dimension.isAll() && dimension != i) {
                continue;
            }

            ParametricKnobDimViewPtr data;
            CurvePtr curve = getParametricCurveInternal(DimIdx(i), *it, &data);
            if (!curve) {
                continue;
            }
            for (std::list<double>::const_iterator it2 = times.begin(); it2!=times.end(); ++it2) {
                KeyFrame k;
                if (curve->setKeyFrameInterpolation(interpolation, TimeValue(*it2), &k)) {
                    if (newKeys) {
                        newKeys->push_back(k);
                    }
                }
            }
            signalCurveChanged(dimension, data);
        }
    }

    evaluateValueChange(dimension, getCurrentRenderTime(), view, eValueChangedReasonUserEdited);
}

bool
KnobParametric::setLeftAndRightDerivativesAtTime(ViewSetSpec view, DimSpec dimension, TimeValue time, double left, double right)
{
    std::list<ViewIdx> views = getViewsList();
    int nDims = getNDimensions();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (int i = 0; i < nDims; ++i) {
            if (!dimension.isAll() && dimension != i) {
                continue;
            }

            ParametricKnobDimViewPtr data;
            CurvePtr curve = getParametricCurveInternal(DimIdx(i), *it, &data);
            if (!curve) {
                continue;
            }

            int keyIndex = curve->keyFrameIndex(time);
            if (keyIndex == -1) {
                return false;
            }

            curve->setKeyFrameInterpolation(eKeyframeTypeFree, keyIndex);
            curve->setKeyFrameDerivatives(left, right, keyIndex);

            signalCurveChanged(dimension, data);
        }

    }

    evaluateValueChange(dimension, getCurrentRenderTime(), view, eValueChangedReasonUserEdited);
    return true;
}

bool
KnobParametric::setDerivativeAtTime(ViewSetSpec view, DimSpec dimension, TimeValue time, double derivative, bool isLeft)
{

    std::list<ViewIdx> views = getViewsList();
    int nDims = getNDimensions();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (int i = 0; i < nDims; ++i) {
            if (!dimension.isAll() && dimension != i) {
                continue;
            }

            ParametricKnobDimViewPtr data;
            CurvePtr curve = getParametricCurveInternal(DimIdx(i), *it, &data);
            if (!curve) {
                continue;
            }

            int keyIndex = curve->keyFrameIndex(time);
            if (keyIndex == -1) {
                return false;
            }

            curve->setKeyFrameInterpolation(eKeyframeTypeBroken, keyIndex);
            if (isLeft) {
                curve->setKeyFrameLeftDerivative(derivative, keyIndex);
            } else {
                curve->setKeyFrameRightDerivative(derivative, keyIndex);
            }

            signalCurveChanged(dimension, data);

        }
    }
    evaluateValueChange(dimension, getCurrentRenderTime(), view, eValueChangedReasonUserEdited);
    return true;
}

ValueChangedReturnCodeEnum
KnobParametric::setKeyFrameInternal(TimeValue time, double value, DimIdx dimension, ViewIdx view, KeyFrame* newKey)
{
    ParametricKnobDimViewPtr data;
    CurvePtr curve = getParametricCurveInternal(dimension, view, &data);
    if (!curve) {
        return eValueChangedReturnCodeNothingChanged;
    }

    ValueChangedReturnCodeEnum ret = curve->setOrAddKeyframe(KeyFrame(time, value));
    if (newKey) {
        (void)curve->getKeyFrameWithTime(time, newKey);
    }
    signalCurveChanged(dimension, data);
    return ret;
}

ValueChangedReturnCodeEnum
KnobParametric::setDoubleValueAtTime(TimeValue time, double value, ViewSetSpec view, DimSpec dimension, ValueChangedReasonEnum reason, KeyFrame* newKey)
{
    ValueChangedReturnCodeEnum ret = eValueChangedReturnCodeNothingChanged;
    std::list<ViewIdx> views = getViewsList();
    int nDims = getNDimensions();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (int i = 0; i < nDims; ++i) {
            if (!dimension.isAll() && dimension != i) {
                continue;
            }

            ret = setKeyFrameInternal(time, value, DimIdx(i), *it, newKey);
            
        }
    }

    if (ret != eValueChangedReturnCodeNothingChanged) {
        evaluateValueChange(dimension, getCurrentRenderTime(), view, reason);
    }
    return ret;
}

void
KnobParametric::setMultipleDoubleValueAtTime(const std::list<DoubleTimeValuePair>& keys, ViewSetSpec view, DimSpec dimension, ValueChangedReasonEnum reason, std::vector<KeyFrame>* newKey)
{
    if (keys.empty()) {
        return;
    }
    if (newKey) {
        newKey->clear();
    }

    std::list<ViewIdx> views = getViewsList();
    int nDims = getNDimensions();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (int i = 0; i < nDims; ++i) {
            if (!dimension.isAll() && dimension != i) {
                continue;
            }
            KeyFrame key;
            for (std::list<DoubleTimeValuePair>::const_iterator it2 = keys.begin(); it2!=keys.end(); ++it2) {
                setKeyFrameInternal(it2->time, it2->value, DimIdx(i), *it, newKey ? &key : 0);
                if (newKey) {
                    newKey->push_back(key);
                }
            }

        }
    }

    evaluateValueChange(dimension, getCurrentRenderTime(), view, reason);
}

void
KnobParametric::setDoubleValueAtTimeAcrossDimensions(TimeValue time, const std::vector<double>& values, DimIdx dimensionStartIndex, ViewSetSpec view, ValueChangedReasonEnum reason, std::vector<ValueChangedReturnCodeEnum>* retCodes)
{
    if (values.empty()) {
        return;
    }
    
    if (dimensionStartIndex < 0 || dimensionStartIndex + values.size() > _imp->common->defaultCurves.size()) {
        throw std::invalid_argument("KnobParametric: dimension out of range");
    }
    std::list<ViewIdx> views = getViewsList();
    ViewIdx view_i;
    if (!view.isAll()) {
        view_i = checkIfViewExistsOrFallbackMainView(ViewIdx(view));
    }
    for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
        if (!view.isAll()) {
            if (view_i != *it) {
                continue;
            }
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            ValueChangedReturnCodeEnum ret = setKeyFrameInternal(time, values[i], DimIdx(dimensionStartIndex + i), *it, 0);
            if (retCodes) {
                retCodes->push_back(ret);
            }
        }
    }

    evaluateValueChange(DimSpec::all(), getCurrentRenderTime(), view, reason);

}

void
KnobParametric::setMultipleDoubleValueAtTimeAcrossDimensions(const PerCurveDoubleValuesList& keysPerDimension, ValueChangedReasonEnum reason)
{
    if (keysPerDimension.empty()) {
        return;
    }
    for (std::size_t i = 0; i < keysPerDimension.size(); ++i) {
        if (keysPerDimension[i].second.empty()) {
            continue;
        }
        for (std::list<DoubleTimeValuePair>::const_iterator it2 = keysPerDimension[i].second.begin(); it2!=keysPerDimension[i].second.end(); ++it2) {
            setKeyFrameInternal(it2->time, it2->value, keysPerDimension[i].first.dimension, keysPerDimension[i].first.view, 0);
        }

    }

    evaluateValueChange(DimSpec::all(), getCurrentRenderTime(), ViewSetSpec(0), reason);
}

bool
KnobParametric::canLinkWith(const KnobIPtr & other, DimIdx /*thisDimension*/, ViewIdx /*thisView*/,  DimIdx /*otherDim*/, ViewIdx /*otherView*/, std::string* error) const
{
    KnobParametric* otherIsParametric = dynamic_cast<KnobParametric*>(other.get());
    if (!otherIsParametric) {
        if (error) {
            *error = tr("Can only link with another parametric curve").toStdString();
        }
        return false;
    }
    return true;
}

void
KnobParametric::onLinkChanged()
{
    Q_EMIT curveChanged(DimSpec::all());
}

/******************************KnobTable**************************************/


KnobTable::KnobTable(const KnobHolderPtr& holder,
                     const std::string &name,
                     int dimension)
    : KnobStringBase(holder, name, dimension)
{
}


KnobTable::KnobTable(const KnobHolderPtr& holder,
          const KnobIPtr& mainInstance)
: KnobStringBase(holder, mainInstance)
{

}

KnobTable::~KnobTable()
{
}

void
KnobTable::getTableSingleCol(std::list<std::string>* table)
{
    std::list<std::vector<std::string> > tmp;

    getTable(&tmp);
    for (std::list<std::vector<std::string> >::iterator it = tmp.begin(); it != tmp.end(); ++it) {
        table->push_back( (*it)[0] );
    }
}

void
KnobTable::getTable(std::list<std::vector<std::string> >* table)
{
    decodeFromKnobTableFormat(getValue(), table);
}

void
KnobTable::decodeFromKnobTableFormat(const std::string& value,
                                     std::list<std::vector<std::string> >* table)
{
    QString raw = QString::fromUtf8( value.c_str() );

    if ( raw.isEmpty() ) {
        return;
    }
    const int colsCount = getColumnsCount();
    assert(colsCount > 0);


    QString startTag = QString::fromUtf8("<%1>");
    QString endTag = QString::fromUtf8("</%1>");
    int lastFoundIndex = 0;

    for (;; ) {
        int colIndex = 0;
        std::vector<std::string> row;
        bool mustStop = false;
        while (colIndex < colsCount) {
            QString colLabel = QString::fromUtf8( getColumnLabel(colIndex).c_str() );
            const QString startToFind = startTag.arg(colLabel);
            const QString endToFind = endTag.arg(colLabel);

            lastFoundIndex = raw.indexOf(startToFind, lastFoundIndex);
            if (lastFoundIndex == -1) {
                mustStop = true;
                break;
            }

            lastFoundIndex += startToFind.size();
            assert( lastFoundIndex < raw.size() );

            int endNamePos = raw.indexOf(endToFind, lastFoundIndex);
            assert( endNamePos != -1 && endNamePos < raw.size() );

            if ( (endNamePos == -1) || ( endNamePos >= raw.size() ) ) {
                KnobHolderPtr holder = getHolder();
                QString knobName;
                if (holder) {
                    EffectInstancePtr effect = toEffectInstance(holder);
                    if (effect) {
                        knobName += QString::fromUtf8(effect->getNode()->getFullyQualifiedName().c_str());
                        knobName += QString::fromUtf8(".");
                    }
                }
                knobName += QString::fromUtf8(getName().c_str());
                QString err = tr("%1 table is wrongly encoded, check your project file or report an issue to the developers").arg(knobName);
                throw std::logic_error(err.toStdString());
            }

            std::string value = raw.mid(lastFoundIndex, endNamePos - lastFoundIndex).toStdString();
            lastFoundIndex += (endNamePos - lastFoundIndex);


            // In order to use XML tags, the text inside the tags has to be unescaped.
            value = Project::unescapeXML(value);

            row.push_back(value);

            ++colIndex;
        }

        if (mustStop) {
            break;
        }

        if ( (int)row.size() == colsCount ) {
            table->push_back(row);
        } else {
            KnobHolderPtr holder = getHolder();
            QString knobName;
            if (holder) {
                EffectInstancePtr effect = toEffectInstance(holder);
                if (effect) {
                    knobName += QString::fromUtf8(effect->getNode()->getFullyQualifiedName().c_str());
                    knobName += QString::fromUtf8(".");
                }
            }
            knobName += QString::fromUtf8(getName().c_str());
            QString err = tr("%1 table is wrongly encoded, check your project file or report an issue to the developers").arg(knobName);
            throw std::logic_error(err.toStdString());

        }
    }
} // KnobTable::decodeFromKnobTableFormat

std::string
KnobTable::encodeToKnobTableFormatSingleCol(const std::list<std::string>& table)
{
    std::list<std::vector<std::string> > tmp;

    for (std::list<std::string>::const_iterator it = table.begin(); it != table.end(); ++it) {
        std::vector<std::string> vec;
        vec.push_back(*it);
        tmp.push_back(vec);
    }

    return encodeToKnobTableFormat(tmp);
}

std::string
KnobTable::encodeToKnobTableFormat(const std::list<std::vector<std::string> >& table)
{
    std::stringstream ss;


    for (std::list<std::vector<std::string> >::const_iterator it = table.begin(); it != table.end(); ++it) {
        // In order to use XML tags, the text inside the tags has to be escaped.
        for (std::size_t c = 0; c < it->size(); ++c) {
            std::string label = getColumnLabel(c);
            ss << "<" << label << ">";
            ss << Project::escapeXML( (*it)[c] );
            ss << "</" << label << ">";
        }
    }

    return ss.str();
}

void
KnobTable::setTableSingleCol(const std::list<std::string>& table)
{
    std::list<std::vector<std::string> > tmp;

    for (std::list<std::string>::const_iterator it = table.begin(); it != table.end(); ++it) {
        std::vector<std::string> vec;
        vec.push_back(*it);
        tmp.push_back(vec);
    }
    setTable(tmp);
}

void
KnobTable::setTable(const std::list<std::vector<std::string> >& table)
{
    setValue(encodeToKnobTableFormat(table));
}

void
KnobTable::appendRowSingleCol(const std::string& row)
{
    std::vector<std::string> tmp;

    tmp.push_back(row);
    appendRow(tmp);
}

void
KnobTable::appendRow(const std::vector<std::string>& row)
{
    std::list<std::vector<std::string> > table;

    getTable(&table);
    table.push_back(row);
    setTable(table);
}

void
KnobTable::insertRowSingleCol(int index,
                              const std::string& row)
{
    std::vector<std::string> tmp;

    tmp.push_back(row);
    insertRow(index, tmp);
}

void
KnobTable::insertRow(int index,
                     const std::vector<std::string>& row)
{
    std::list<std::vector<std::string> > table;

    getTable(&table);
    if ( (index < 0) || ( index >= (int)table.size() ) ) {
        table.push_back(row);
    } else {
        std::list<std::vector<std::string> >::iterator pos = table.begin();
        std::advance(pos, index);
        table.insert(pos, row);
    }

    setTable(table);
}

void
KnobTable::removeRow(int index)
{
    std::list<std::vector<std::string> > table;

    getTable(&table);
    if ( (index < 0) || ( index >= (int)table.size() ) ) {
        return;
    }
    std::list<std::vector<std::string> >::iterator pos = table.begin();
    std::advance(pos, index);
    table.erase(pos);
    setTable(table);
}

const std::string KnobLayers::_typeNameStr(kKnobLayersTypeName);
const std::string&
KnobLayers::typeNameStatic()
{
    return _typeNameStr;
}


std::string
KnobLayers::encodePlanesList(const std::list<ImagePlaneDesc>& planes)
{
    std::list<std::vector<std::string> > layerStrings;
    for (std::list<ImagePlaneDesc>::const_iterator it = planes.begin(); it!=planes.end();++it) {
        const ImagePlaneDesc& comps = *it;
        std::vector<std::string> row(3);
        row[0] = comps.getPlaneLabel();
        std::string channelsStr;
        const std::vector<std::string>& channels = comps.getChannels();
        for (std::size_t c = 0; c < channels.size(); ++c) {
            if (c > 0) {
                channelsStr += ' ';
            }
            channelsStr += channels[c];
        }
        row[1] = channelsStr;
        row[2] = comps.getChannelsLabel();
        layerStrings.push_back(row);
    }
    return encodeToKnobTableFormat(layerStrings);
}

std::list<ImagePlaneDesc>
KnobLayers::decodePlanesList() 
{
    std::list<ImagePlaneDesc> ret;

    std::list<std::vector<std::string> > table;
    getTable(&table);
    for (std::list<std::vector<std::string> >::iterator it = table.begin();
         it != table.end(); ++it) {

        const std::string& planeLabel = (*it)[0];
        std::string planeID = planeLabel;

        // The layers knob only propose the user to display the label of the plane desc,
        // but we need to recover the ID for the built-in planes to ensure compatibility
        // with the old Nuke multi-plane suite.
        if (planeID == kNatronColorPlaneLabel) {
            planeID = kNatronColorPlaneID;
        } else if (planeID == kNatronBackwardMotionVectorsPlaneLabel) {
            planeID = kNatronBackwardMotionVectorsPlaneID;
        } else if (planeID == kNatronForwardMotionVectorsPlaneLabel) {
            planeID = kNatronForwardMotionVectorsPlaneID;
        } else if (planeID == kNatronDisparityLeftPlaneLabel) {
            planeID = kNatronDisparityLeftPlaneID;
        } else if (planeID == kNatronDisparityRightPlaneLabel) {
            planeID = kNatronDisparityRightPlaneID;
        }

        bool found = false;
        for (std::list<ImagePlaneDesc>::const_iterator it2 = ret.begin(); it2 != ret.end(); ++it2) {
            if (it2->getPlaneID() == planeID) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::vector<std::string> componentsName;
            QString str = QString::fromUtf8( (*it)[1].c_str() );
            QStringList channels = str.split( QLatin1Char(' ') );
            componentsName.resize( channels.size() );
            for (int i = 0; i < channels.size(); ++i) {
                componentsName[i] = channels[i].toStdString();
            }

            const std::string& componentsLabel = (*it)[2];
            ImagePlaneDesc c( planeID, planeLabel, componentsLabel, componentsName );
            ret.push_back(c);
        }
    }

    return ret;
} // decodePlanesList


NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_KnobTypes.cpp"
