// ======================================================================
//
// PaletteColorCustomizationVariable.h
// Copyright 2002 Sony Online Entertainment, Inc.
// All Rights Reserved.
//
// ======================================================================

#ifndef INCLUDED_PaletteColorCustomizationVariable_H
#define INCLUDED_PaletteColorCustomizationVariable_H

// ======================================================================

#include "sharedFoundation/MemoryBlockManagerMacros.h"
#include "sharedObject/RangedIntCustomizationVariable.h"
#include "sharedMath/PackedArgb.h"

class MemoryBlockManager;
class PaletteArgb;

// ======================================================================

class PaletteColorCustomizationVariable: public RangedIntCustomizationVariable
{
	MEMORY_BLOCK_MANAGER_INTERFACE_WITH_INSTALL;

public:

	PaletteColorCustomizationVariable(const PaletteArgb *palette, int selectedIndex = 0);
	virtual ~PaletteColorCustomizationVariable();

	virtual int          getValue() const;
	virtual bool         setValue(int value);
	const PackedArgb    &getValueAsColor() const;

	virtual void         getRange(int &minRangeInclusive, int &maxRangeExclusive) const;

	virtual void         writeObjectTemplateExportString(const std::string &variablePathName, ObjectTemplateCustomizationDataWriter &writer) const;

	// Persistence overrides for direct color support
	virtual int          getPersistedDataByteCount() const;
	virtual void         saveToByteVector(ByteVector &data) const;
	virtual bool         restoreFromByteVector(ByteVector const &data, int startIndex, int length);

	const PaletteArgb   *fetchPalette() const;

	void                 setClosestColor(const PackedArgb &targetColor);

	// Direct color support - HTML hex codes and RGB values
	bool                 setDirectColor(const PackedArgb &color);
	bool                 setDirectColorHtml(const char *htmlColor);
	bool                 hasDirectColor() const;
	const PackedArgb    &getDirectColor() const;
	int                  getMatchedPaletteIndex() const;

	// Static helpers for special index encoding
	static bool          isSpecialColorIndex(int index);
	static int           encodeColorAsIndex(const PackedArgb &color);
	static PackedArgb    decodeColorFromIndex(int specialIndex);

#ifdef _DEBUG
	virtual std::string  debugToString() const;
#endif

private:

	// disabled
	PaletteColorCustomizationVariable();
	PaletteColorCustomizationVariable(const PaletteColorCustomizationVariable&);
	PaletteColorCustomizationVariable &operator =(const PaletteColorCustomizationVariable&);

private:

	const PaletteArgb *const m_palette;
	int                      m_paletteIndex;
	bool                     m_useDirectColor;
	mutable PackedArgb       m_directColor;

};

// ======================================================================

#endif
