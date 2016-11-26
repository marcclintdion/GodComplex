#pragma once

using namespace System;

#include "ColorProfile.h"

namespace ImageUtility {

	ref class ImageFile;

	// Holds the image's color profile as well as important shot information pulled from EXIF data
	// You may want to have a look at APEX format to understand Tv and Av settings (https://en.wikipedia.org/wiki/APEX_system)
	//
	public ref class MetaData {
	private:
		ImageUtilityLib::MetaData*	m_nativeObject;

	public:	// PROPERTIES

		property ImageUtility::ColorProfile^		ColorProfile {
			ImageUtility::ColorProfile^	get() {
				return gcnew ImageUtility::ColorProfile( m_nativeObject->GetColorProfile() );
			}
			void	set( ImageUtility::ColorProfile^ value ) {
				if ( value == nullptr )
					throw gcnew Exception( "Invalid null profile!" );
				m_nativeObject->SetColorProfile( *value->m_nativeObject );
			}
		}

		// True if the gamma exponent was found in the file
		property bool		GammaSpecifiedInFile {
			bool	get() { return m_nativeObject->m_gammaSpecifiedInFile; }
		}

		// True if the following information was found in the file (sometimes not available from older file formats like GIF or BMP)
		property bool		IsValid {
			bool	get() { return m_nativeObject->m_valid; }
		}
		// ISO speed (min = 50)
		property UInt32		ISOSpeed {
			UInt32	get() { return m_nativeObject->m_ISOSpeed; }
		}
		// Exposure time (in seconds)
		property float		ExposureTime {
			float	get() { return m_nativeObject->m_exposureTime; }
		}
		// Shutter Speed Value, in EV (Tv = log2( 1/ShutterSpeed))
		property float		Tv {
			float	get() { return m_nativeObject->m_Tv; }
		}
		// Aperture Value, in EV (Av = log2( Aperture� ))
		property float		Av {
			float	get() { return m_nativeObject->m_Av; }
		}
		// In F-stops
		property float		FNumber {
			float	get() { return m_nativeObject->m_FNumber; }
		}
		// In mm
		property float		FocalLength {
			float	get() { return m_nativeObject->m_focalLength; }
		}


	internal:	// METHODS
		MetaData() : m_nativeObject( nullptr ) {
		}

		MetaData( ImageFile^ _owner );
	};
}
