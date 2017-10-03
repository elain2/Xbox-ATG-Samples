//--------------------------------------------------------------------------------------
//
// ISACRenderer.cpp
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------


#include "pch.h"
#include "ISACRenderer.h"

using namespace Windows::Media::Devices;

using Microsoft::WRL::ComPtr;

//
//  ISACRenderer()
//
ISACRenderer::ISACRenderer() :

	m_SpatialAudioClient( nullptr ),
	m_SpatialAudioStream( nullptr ),
	m_ISACrenderstate( RenderState::Inactive ),
	m_bufferCompletionEvent( NULL )
{

}

//
//  ~ISACRenderer()
//
ISACRenderer::~ISACRenderer()
{
}

//
//  InitializeAudioDeviceAsync()
//
//  Activates the default audio renderer on a asynchronous callback thread.  This needs
//  to be called from the main UI thread.
//
HRESULT ISACRenderer::InitializeAudioDeviceAsync()
{
	ComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;
    HRESULT hr = S_OK;

    // Get a string representing the Default Audio Device Renderer
    m_DeviceIdString = MediaDevice::GetDefaultAudioRenderId( Windows::Media::Devices::AudioDeviceRole::Default );

	EnableSpatialAudio();

	// This call must be made on the main UI thread.  Async operation will call back to 
    // IActivateAudioInterfaceCompletionHandler::ActivateCompleted, which must be an agile interface implementation
	hr = ActivateAudioInterfaceAsync(m_DeviceIdString->Data(), __uuidof(ISpatialAudioClient), nullptr, this, &asyncOp);

	if (FAILED( hr ))
    {
		m_ISACrenderstate = RenderState::Inactive;
	}

    return hr;
}

//When Spatial dynamic object count changes, note new number of max available objects
HRESULT ISACRenderer::OnAvailableDynamicObjectCountChange(ISpatialAudioObjectRenderStreamBase *sender, LONGLONG hnsComplianceDeadlineTime, UINT32 availableDynamicObjectCountChange)
{
	sender;
	hnsComplianceDeadlineTime;

	m_MaxDynamicObjects = availableDynamicObjectCountChange;
	return S_OK;
}

//
//  ActivateCompleted()
//
//  Callback implementation of ActivateAudioInterfaceAsync function.  This will be called on MTA thread
//  when results of the activation are available.
//
HRESULT ISACRenderer::ActivateCompleted( IActivateAudioInterfaceAsyncOperation *operation )
{
    HRESULT hr = S_OK;
    HRESULT hrActivateResult = S_OK;
	ComPtr<IUnknown> punkAudioInterface = nullptr;

    // Check for a successful activation result
    hr = operation->GetActivateResult( &hrActivateResult, &punkAudioInterface );
    if (SUCCEEDED( hr ) && SUCCEEDED( hrActivateResult ))
    {
		// Get the pointer for the Audio Client
        punkAudioInterface.Get()->QueryInterface( IID_PPV_ARGS(&m_SpatialAudioClient) );
        if( nullptr == m_SpatialAudioClient)
        {
            hr = E_FAIL;
            goto exit;
        }

		// Check the available rendering formats 
		ComPtr<IAudioFormatEnumerator> audioObjectFormatEnumerator;
		hr = m_SpatialAudioClient->GetSupportedAudioObjectFormatEnumerator(&audioObjectFormatEnumerator);

		// WavFileIO is helper class to read WAV file
		WAVEFORMATEX* objectFormat = nullptr;
		UINT32 audioObjectFormatCount;
		hr = audioObjectFormatEnumerator->GetCount(&audioObjectFormatCount); // There is at least one format that the API accept
		if (audioObjectFormatCount == 0)
		{
			hr = E_FAIL;
			goto exit;
		}
		 
		// Select the most favorable format, first one
		hr = audioObjectFormatEnumerator->GetFormat(0, &objectFormat);

		// Create the event that will be used to signal the client for more data
		m_bufferCompletionEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		// Define the object mask for the Spatial bed - in this case 7.1.4
		AudioObjectType objectmask =
			AudioObjectType_FrontLeft |
			AudioObjectType_FrontRight |
			AudioObjectType_FrontCenter |
			AudioObjectType_LowFrequency |
			AudioObjectType_SideLeft |
			AudioObjectType_SideRight |
			AudioObjectType_BackLeft |
			AudioObjectType_BackRight |
			AudioObjectType_TopFrontLeft |
			AudioObjectType_TopFrontRight |
			AudioObjectType_TopBackLeft |
			AudioObjectType_TopBackRight;

		// Determine maximum available dynamic objects
		m_SpatialAudioClient->GetMaxDynamicObjectCount(&m_MaxDynamicObjects);

		// Set the activation parameters for the Spatial stream
		SpatialAudioObjectRenderStreamActivationParams  activationparams =
		{
			objectFormat,
			objectmask,
			m_MaxDynamicObjects,
			m_MaxDynamicObjects,
			AudioCategory_GameEffects,
			m_bufferCompletionEvent,
			nullptr
		};

		PROPVARIANT activateParams;
		PropVariantInit(&activateParams);
		activateParams.vt = VT_BLOB;
		activateParams.blob.cbSize = sizeof(activationparams);
		activateParams.blob.pBlobData = reinterpret_cast<BYTE*>(&activationparams);

		// Activate the spatial stream
		hr = m_SpatialAudioClient->ActivateSpatialAudioStream(&activateParams, __uuidof(m_SpatialAudioStream), &m_SpatialAudioStream);
		if (FAILED(hr))
		{
			hr = E_FAIL;
			goto exit;
		}

		// Start streaming / rendering� 
		hr = m_SpatialAudioStream->Start();
		if (FAILED(hr))
		{
			hr = E_FAIL;
			goto exit;
		}

		m_ISACrenderstate = RenderState::Active;

	}

exit:

    if (FAILED( hr ))
    {
		m_ISACrenderstate = RenderState::Inactive;
	}
   
    // Need to return S_OK
    return S_OK;
}

