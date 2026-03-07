// ======================================================================
//
// PaletteColorCustomizationVariable.cpp
// Copyright 2002 Sony Online Entertainment Inc.
// All Rights Reserved.
//
// ======================================================================

#include "sharedObject/FirstSharedObject.h"
#include "sharedObject/PaletteColorCustomizationVariable.h"

#include "sharedFoundation/ExitChain.h"
#include "sharedFoundation/MemoryBlockManager.h"
#include "sharedMath/PackedArgb.h"
#include "sharedMath/PaletteArgb.h"
#include "sharedObject/ObjectTemplateCustomizationDataWriter.h"

#include <string>
#include <cstdio>

// ======================================================================

MEMORY_BLOCK_MANAGER_IMPLEMENTATION_WITH_INSTALL(PaletteColorCustomizationVariable, true, 0, 0, 0);

// ======================================================================

PaletteColorCustomizationVariable::PaletteColorCustomizationVariable(const PaletteArgb *palette, int selectedIndex)
:	RangedIntCustomizationVariable(),
	m_palette(palette),
	m_paletteIndex(selectedIndex),
	m_useDirectColor(false),
	m_directColor(PackedArgb::solidWhite)
{
	NOT_NULL(palette);
	palette->fetch();

	int const paletteEntryCount = m_palette->getEntryCount();

	// Check if this is a special color-encoded index
	if (isSpecialColorIndex(selectedIndex))
	{
		m_directColor = decodeColorFromIndex(selectedIndex);
		m_useDirectColor = true;
		// Find closest palette match for rendering
		m_paletteIndex = m_palette->findClosestMatch(m_directColor);
	}
	else if ((m_paletteIndex < 0) || (m_paletteIndex >= paletteEntryCount))
	{
		DEBUG_WARNING(true, ("Initializing palette var for palette=[%s] with out-of-range value [%d], valid range is [0..%d], defaulting to [%d].", m_palette->getName().getString(), m_paletteIndex, paletteEntryCount - 1, paletteEntryCount - 1));
		m_paletteIndex = paletteEntryCount - 1;
	}
}

// ----------------------------------------------------------------------

PaletteColorCustomizationVariable::~PaletteColorCustomizationVariable()
{
	//lint -esym(1540, PaletteColorCustomizationVariable::m_palette) // not freed or zero'ed  // yes, but released
	m_palette->release();
}

// ----------------------------------------------------------------------

int PaletteColorCustomizationVariable::getValue() const
{
	// If using direct color, return the special encoded index
	if (m_useDirectColor)
	{
		return encodeColorAsIndex(m_directColor);
	}
	return m_paletteIndex;
}

// ----------------------------------------------------------------------

bool PaletteColorCustomizationVariable::setValue(int value)
{
	// Check if this is a special color-encoded index
	if (isSpecialColorIndex(value))
	{
		m_directColor = decodeColorFromIndex(value);
		m_useDirectColor = true;
		// Find closest palette match for rendering
		m_paletteIndex = m_palette->findClosestMatch(m_directColor);
		signalVariableModified();
		return RangedIntCustomizationVariable::setValue(m_paletteIndex);
	}

	// Traditional palette index
	if ((value >= 0) && (value < m_palette->getEntryCount()))
	{
		// change the value
		m_paletteIndex = value;
		m_useDirectColor = false;

		// notify owner CustomizationData about change.
		signalVariableModified();
	}
	else
	{
		DEBUG_WARNING(true, ("attempted to set PaletteColorCustomizationVariable to %d, outside valid range [%d,%d).", value, 0, m_palette->getEntryCount()));
		return false;
	}

	return RangedIntCustomizationVariable::setValue(value);
}

// ----------------------------------------------------------------------

const PackedArgb &PaletteColorCustomizationVariable::getValueAsColor() const
{
	// If using direct color, return the matched palette color for rendering compatibility
	// Use getDirectColor() to get the actual selected color
	if (!m_palette)
		return PackedArgb::solidMagenta;

	int const usePaletteIndex = clamp(0, m_paletteIndex, m_palette->getEntryCount() - 1);
	bool error = false;
	PackedArgb const & color = m_palette->getEntry(usePaletteIndex, error);

	WARNING(error, ("PaletteColorCustomizationVariable::getValueAsColor error"));
	return color;
}

// ----------------------------------------------------------------------

void PaletteColorCustomizationVariable::getRange(int &minRangeInclusive, int &maxRangeExclusive) const
{
	minRangeInclusive = 0;
	maxRangeExclusive = m_palette->getEntryCount();
}

// ----------------------------------------------------------------------

const PaletteArgb *PaletteColorCustomizationVariable::fetchPalette() const
{
	m_palette->fetch();
	return m_palette;
}

// ----------------------------------------------------------------------

void PaletteColorCustomizationVariable::setClosestColor(const PackedArgb &targetColor)
{
	setValue(m_palette->findClosestMatch(targetColor));
}

// ----------------------------------------------------------------------

void PaletteColorCustomizationVariable::writeObjectTemplateExportString(const std::string &variablePathName, ObjectTemplateCustomizationDataWriter &writer) const
{
#if !(USE_OBJ_TEMPLATE_CUSTOM_VAR_WRITER)
	UNREF(variablePathName);
	UNREF(writer);
#else
	//-- this assumes that the current value is the default value --- if the user changed values at all, they
	//   will not be the artist's built in default.
	writer.addPaletteColorCustomizationVariable(variablePathName, m_palette->getName().getString(), m_paletteIndex);
#endif
}

// ----------------------------------------------------------------------

#ifdef _DEBUG

std::string PaletteColorCustomizationVariable::debugToString() const
{
	char buffer[1024];

	if (m_useDirectColor)
	{
		char htmlColor[16];
		m_directColor.toHtmlString(htmlColor, sizeof(htmlColor), false);
		sprintf(buffer, "palette (%s): direct color %s -> matched index %d", m_palette->getName().getString(), htmlColor, m_paletteIndex);
	}
	else
	{
		sprintf(buffer, "palette (%s): index %d", m_palette->getName().getString(), m_paletteIndex);
	}
	return std::string(buffer);
}

#endif

// ----------------------------------------------------------------------
/**
 * Set a direct RGB color, bypassing the palette.
 * The color will be auto-matched to the closest palette entry for rendering.
 * @param color  The direct color to set
 * @return  true if successful
 */
bool PaletteColorCustomizationVariable::setDirectColor(const PackedArgb &color)
{
	m_directColor = color;
	m_useDirectColor = true;
	m_paletteIndex = m_palette->findClosestMatch(color);
	signalVariableModified();
	return true;
}

// ----------------------------------------------------------------------
/**
 * Set a direct color from an HTML color string.
 * @param htmlColor  The HTML color string (e.g., "#FF5500")
 * @return  true if the string was valid and color was set
 */
bool PaletteColorCustomizationVariable::setDirectColorHtml(const char *htmlColor)
{
	if (!PackedArgb::isValidHtmlColor(htmlColor))
		return false;

	PackedArgb color = PackedArgb::fromHtmlString(htmlColor);
	return setDirectColor(color);
}

// ----------------------------------------------------------------------
/**
 * Check if this variable is using a direct color vs palette index.
 * @return  true if using direct color
 */
bool PaletteColorCustomizationVariable::hasDirectColor() const
{
	return m_useDirectColor;
}

// ----------------------------------------------------------------------
/**
 * Get the direct color value (the actual selected color, not the matched palette color).
 * @return  The direct color, or the palette color if not using direct color
 */
const PackedArgb &PaletteColorCustomizationVariable::getDirectColor() const
{
	if (m_useDirectColor)
		return m_directColor;

	// Return the palette color
	return getValueAsColor();
}

// ----------------------------------------------------------------------
/**
 * Get the palette index that was matched to the direct color.
 * @return  The matched palette index
 */
int PaletteColorCustomizationVariable::getMatchedPaletteIndex() const
{
	return m_paletteIndex;
}

// ----------------------------------------------------------------------
/**
 * Check if an index is a special color-encoded index.
 * @param index  The index to check
 * @return  true if this is a special color index (negative)
 */
bool PaletteColorCustomizationVariable::isSpecialColorIndex(int index)
{
	return PackedArgb::isSpecialColorIndex(index);
}

// ----------------------------------------------------------------------
/**
 * Encode a color as a special negative palette index.
 * @param color  The color to encode
 * @return  The special encoded index
 */
int PaletteColorCustomizationVariable::encodeColorAsIndex(const PackedArgb &color)
{
	return PackedArgb::encodeAsSpecialIndex(color);
}

// ----------------------------------------------------------------------
/**
 * Decode a color from a special encoded index.
 * @param specialIndex  The special index to decode
 * @return  The decoded color
 */
PackedArgb PaletteColorCustomizationVariable::decodeColorFromIndex(int specialIndex)
{
	return PackedArgb::decodeFromSpecialIndex(specialIndex);
}

// ======================================================================
