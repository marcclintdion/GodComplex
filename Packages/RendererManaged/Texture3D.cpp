// This is the main DLL file.

#include "stdafx.h"

#include "Texture3D.h"


namespace Renderer {

	Texture3D::Texture3D( Device^ _device, UInt32 _width, UInt32 _height, UInt32 _depth, UInt32 _mipLevelsCount, PIXEL_FORMAT _pixelFormat, bool _staging, bool _UAV, cli::array<PixelsBuffer^>^ _mipLevelsContent ) {
 		IPixelFormatDescriptor*	pDescriptor = GetDescriptor( _pixelFormat );

		void**	ppContent = NULL;
		if ( _mipLevelsContent != nullptr ) {
			ppContent = new void*[_mipLevelsCount];
			pin_ptr<Byte>	ptr;
			for ( UInt32 MipLevelIndex=0; MipLevelIndex < _mipLevelsCount; MipLevelIndex++ ) {
				ptr = &_mipLevelsContent[MipLevelIndex]->m_Buffer[0];
				ppContent[MipLevelIndex] = ptr;
			}
		}

		m_pTexture = new ::Texture3D( *_device->m_pDevice, _width, _height, _depth, _mipLevelsCount, *pDescriptor, ppContent, _staging, _UAV );

		delete[] ppContent;
	}

	void	Texture3D::Set( UInt32 _slotIndex, View3D^ _view )		{ m_pTexture->Set( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture3D::SetVS( UInt32 _slotIndex, View3D^ _view )	{ m_pTexture->SetVS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture3D::SetHS( UInt32 _slotIndex, View3D^ _view )	{ m_pTexture->SetHS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture3D::SetDS( UInt32 _slotIndex, View3D^ _view )	{ m_pTexture->SetDS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture3D::SetGS( UInt32 _slotIndex, View3D^ _view )	{ m_pTexture->SetGS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture3D::SetPS( UInt32 _slotIndex, View3D^ _view )	{ m_pTexture->SetPS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }
	void	Texture3D::SetCS( UInt32 _slotIndex, View3D^ _view )	{ m_pTexture->SetCS( _slotIndex, true, _view != nullptr ? _view->SRV : NULL ); }


	UInt32							View3D::Width::get() { return m_Owner->Width; }
	UInt32							View3D::Height::get() { return m_Owner->Height; }
	UInt32							View3D::ArraySizeOrDepth::get() { return m_Owner->Depth; }
	::ID3D11ShaderResourceView*		View3D::SRV::get() { return m_AsArray ? m_Owner->m_pTexture->GetSRV( m_MipLevelStart, m_MipLevelsCount, m_SliceStart, m_SlicesCount, m_AsArray ) : m_Owner->m_pTexture->GetSRV( m_MipLevelStart, m_MipLevelsCount ); }
	::ID3D11RenderTargetView*		View3D::RTV::get() { return m_Owner->m_pTexture->GetRTV( m_MipLevelStart, m_SliceStart, m_SlicesCount ); }
	::ID3D11UnorderedAccessView*	View3D::UAV::get() { return m_Owner->m_pTexture->GetUAV( m_MipLevelStart, m_SliceStart, m_SlicesCount ); }
}
