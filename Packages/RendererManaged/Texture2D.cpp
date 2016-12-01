// This is the main DLL file.

#include "stdafx.h"

#include "Texture2D.h"

namespace Renderer {

	Texture2D::Texture2D( Device^ _device, UInt32 _width, UInt32 _height, int _arraySize, UInt32 _mipLevelsCount, PIXEL_FORMAT _pixelFormat, bool _staging, bool _UAV, cli::array<PixelsBuffer^>^ _content ) {
 		IPixelFormatDescriptor*	descriptor = GetDescriptor( _pixelFormat );

		void**	ppContent = NULL;
		if ( _content != nullptr ) {
			pin_ptr< PixelsBuffer^ >	ptr = &_content[0];

			UInt32	arraySize = abs(_arraySize);
			ppContent = new void*[_mipLevelsCount*arraySize];
			for ( UInt32 arrayIndex=0; arrayIndex < arraySize; arrayIndex++ ) {
				for ( UInt32 mipLevelIndex=0; mipLevelIndex < _mipLevelsCount; mipLevelIndex++ ) {
					pin_ptr< Byte >	ptrContent = &_content[arrayIndex*_mipLevelsCount+mipLevelIndex]->m_Buffer[0];
					ppContent[arrayIndex*_mipLevelsCount+mipLevelIndex] = ptrContent;
				}
			}
		}

		m_pTexture = new ::Texture2D( *_device->m_pDevice, _width, _height, _arraySize, _mipLevelsCount, *descriptor, ppContent, _staging, _UAV );

		delete[] ppContent;
	}

	Texture2D::Texture2D( Device^ _device, UInt32 _width, UInt32 _height, UInt32 _arraySize, DEPTH_STENCIL_FORMAT _depthStencilFormat ) {
 		IDepthStencilFormatDescriptor*	pDescriptor = GetDescriptor( _depthStencilFormat );
		m_pTexture = new ::Texture2D( *_device->m_pDevice, _width, _height, _arraySize, *pDescriptor );
	}

	void	Texture2D::Set( UInt32 _slotIndex, View2D^ _view )			{ m_pTexture->Set( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture2D::SetVS( UInt32 _slotIndex, View2D^ _view )		{ m_pTexture->SetVS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture2D::SetHS( UInt32 _slotIndex, View2D^ _view )		{ m_pTexture->SetHS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture2D::SetDS( UInt32 _slotIndex, View2D^ _view )		{ m_pTexture->SetDS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture2D::SetGS( UInt32 _slotIndex, View2D^ _view )		{ m_pTexture->SetGS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture2D::SetPS( UInt32 _slotIndex, View2D^ _view )		{ m_pTexture->SetPS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture2D::SetCS( UInt32 _slotIndex, View2D^ _view )		{ m_pTexture->SetCS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture2D::SetCSUAV( UInt32 _slotIndex, View2D^ _view )		{ m_pTexture->SetCSUAV( _slotIndex, _view != nullptr ? _view->UAV : NULL ); }


	UInt32							View2D::Width::get() { return m_owner->Width; }
	UInt32							View2D::Height::get() { return m_owner->Height; }
	UInt32							View2D::ArraySizeOrDepth::get() { return m_owner->ArraySize; }
	::ID3D11ShaderResourceView*		View2D::SRV::get() { return m_owner->m_pTexture->GetSRV( m_mipLevelStart, m_mipLevelsCount, m_arrayStart, m_arraySize, m_asArray ); }
	::ID3D11RenderTargetView*		View2D::RTV::get() { return m_owner->m_pTexture->GetRTV( m_mipLevelStart, m_arrayStart, m_arraySize ); }
	::ID3D11UnorderedAccessView*	View2D::UAV::get() { return m_owner->m_pTexture->GetUAV( m_mipLevelStart, m_arrayStart, m_arraySize ); }
	::ID3D11DepthStencilView*		View2D::DSV::get() { return m_owner->m_pTexture->GetDSV( m_arrayStart, m_arraySize ); }
}
