
#include <string>

#include "libvideoio_bm/DeckLinkSource.h"

namespace libvideoio_bm {

  using std::string;

  DeckLinkSource::DeckLinkSource()
  : DataSource(),
  _initialized( false ),
  _deckLink( nullptr ),
  _deckLinkInput( nullptr ),
  _deckLinkOutput( nullptr ),
  _inputCallback( nullptr ),
  _outputCallback(nullptr)
  {
    //initialize();
  }

  DeckLinkSource::~DeckLinkSource()
  {
    if( _deckLinkOutput ) {
      // Disable the video input interface
      _deckLinkOutput->DisableVideoOutput();
      _deckLinkOutput->Release();
    }

    if( _deckLinkInput ) {
      _deckLinkInput->StopStreams();
      _deckLinkInput->Release();
    }

  }


  //=================================================================
  // Configuration functions


  bool DeckLinkSource::setDeckLink( int cardno ) {

    if( _deckLink ) _deckLink->Release();

    HRESULT result;

    IDeckLink *deckLink = nullptr;
    IDeckLinkIterator *deckLinkIterator = CreateDeckLinkIteratorInstance();

    // Index cards by number for now
    for( int i = 0; i <= cardno; i++ ) {
      if( (result = deckLinkIterator->Next(&deckLink)) != S_OK) {
        LOG(WARNING) << "Couldn't get information on DeckLink card " << i;
        return false;
      }
    }

    _deckLink = deckLink;
    CHECK( _deckLink != nullptr );

    free( deckLinkIterator );

    char *modelName, *displayName;
    if( _deckLink->GetModelName( (const char **)&modelName ) != S_OK ) {
      LOG(WARNING) << "Unable to query model name.";
    }

    if( _deckLink->GetDisplayName( (const char **)&displayName ) != S_OK ) {
      LOG(WARNING) << "Unable to query display name.";
    }

    LOG(INFO) << "Using card " << cardno << " model name: " << modelName << "; display name: " << displayName;

    free(modelName);
    free(displayName);

    return true;
  }

  bool DeckLinkSource::createVideoInput( const BMDDisplayMode desiredMode, bool do3D )
{
  HRESULT result;

  // Hardcode some parameters for now
  BMDVideoInputFlags inputFlags = bmdVideoInputFlagDefault;
  BMDPixelFormat pixelFormat = bmdFormat10BitYUV;
  //BMDTimecodeFormat m_timecodeFormat;

  // Get the input (capture) interface of the DeckLink device
  result = _deckLink->QueryInterface(IID_IDeckLinkInput, (void**)&_deckLinkInput);
  if (result != S_OK) {
    LOG(WARNING) << "Couldn't get input for Decklink";
    return false;
  }

  IDeckLinkAttributes* deckLinkAttributes = NULL;
  bool formatDetectionSupported;

  // Check the card supports format detection
  result = _deckLink->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes);
  if (result == S_OK)
  {

    // Check for various desired features
    result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &formatDetectionSupported);
    if (result != S_OK || !formatDetectionSupported)
    {
      LOG(WARNING) << "Format detection is not supported on this device";
      return false;
    } else {
      LOG(INFO) << "Enabling automatic format detection on input card.";
      inputFlags |= bmdVideoInputEnableFormatDetection;
    }

  }


  // Format detection still needs a valid mode to start with
  //idx = 0;
  //         }
  //         else
  //         {
  //                 idx = g_config.mdisplayModeIndex;
  //         }


  IDeckLinkDisplayModeIterator* displayModeIterator = NULL;
  IDeckLinkDisplayMode *displayMode = NULL;

  result = _deckLinkInput->GetDisplayModeIterator(&displayModeIterator);
  if (result != S_OK) {
    LOG(WARNING) << "Unable to get DisplayModeIterator";
    return false;
  }

  // Iterate through available modes
  while( displayModeIterator->Next( &displayMode ) == S_OK ) {

    // char *displayModeName = nullptr;
    // if( displayMode->GetName( (const char **)&displayModeName) != S_OK ) {
    //   LOG(WARNING) << "Unable to get name of DisplayMode";
    //   return false;
    // }

    // BMDTimeValue timeValue = 0;
    // BMDTimeScale timeScale = 0;
    //
    // if( displayModeItr->GetFrameRate( &timeValue, &timeScale ) != S_OK ) {
    //   LOG(WARNING) << "Unable to get DisplayMode frame rate";
    //   return;
    // }
    //
    // float frameRate = (timeScale != 0) ? float(timeValue)/timeScale : float(timeValue);
    //
    // LOG(INFO) << "Card supports display mode \"" << displayModeName << "\"    " <<
    // displayModeItr->GetWidth() << " x " << displayModeItr->GetHeight() <<
    // ", " << 1.0/frameRate << " FPS";


    if( displayMode->GetDisplayMode() == desiredMode ) {

      //Check flags
      BMDDisplayModeFlags flags = displayMode->GetFlags();

      if( do3D ) {
        if( !(flags & bmdDisplayModeSupports3D ) )
        {
          LOG(WARNING) << "3D Support requested but not available in this display mode";
        } else {
          LOG(INFO) << "Enabling 3D support detection on input card.";
          inputFlags |= bmdVideoInputDualStream3D;
        }

      }

      // Check display mode is supported with given options
      BMDDisplayModeSupport displayModeSupported;
      result = _deckLinkInput->DoesSupportVideoMode(displayMode->GetDisplayMode(),
                                                    pixelFormat,
                                                    inputFlags,
                                                    &displayModeSupported, NULL);

      if (result != S_OK) {
        LOG(WARNING) << "Error checking if DeckLinkInput supports this mode";
         return false;
      }

      if (displayModeSupported == bmdDisplayModeNotSupported)
      {
        LOG(WARNING) <<  "The display mode is not supported with the selected pixel format on this input";
        return false;
      }

      // If you've made it here, great
      break;
    }

    // free( displayModeName );
  }

  free( displayModeIterator );

  if( ! _deckLinkInput ) {
    // Failed to find a good mode
    LOG(FATAL) << "Unable to find a video input mode with the desired properties";
    goto bail;
  }

  // Configure the capture callback ... needs an output to create frames for conversion
  CHECK( _deckLinkOutput != nullptr );
  _inputCallback = new InputCallback( _deckLinkInput, _deckLinkOutput,  displayMode );
  _deckLinkInput->SetCallback(_inputCallback);

  // Made it this far?  Great!
  result = _deckLinkInput->EnableVideoInput(displayMode->GetDisplayMode(),
                                            pixelFormat,
                                            inputFlags);
  if (result != S_OK)
  {
    LOG(WARNING) << "Failed to enable video input. Is another application using the card?";
    goto bail;
  }

  return true;

bail:
  free( displayMode );

    return false;
  }


  bool DeckLinkSource::createVideoOutput( const BMDDisplayMode desiredMode, bool do3D )
  {
    // Video mode parameters
//    const BMDDisplayMode      kDisplayMode = bmdModeHD1080i50;
   BMDVideoOutputFlags outputFlags  = bmdVideoOutputVANC;
   IDeckLinkDisplayMode *displayMode = nullptr;

    HRESULT result;

    // Obtain the output interface for the DeckLink device
    {
      IDeckLinkOutput *deckLinkOutput = nullptr;
      result = _deckLink->QueryInterface(IID_IDeckLinkOutput, (void**)&deckLinkOutput);
      if(result != S_OK)
      {
        LOGF(WARNING, "Could not obtain the IDeckLinkInput interface - result = %08x\n", result);
        return false;
      }

      _deckLinkOutput = deckLinkOutput;
    }
    CHECK( _deckLinkOutput != nullptr );

    if( do3D ) {
      outputFlags |= bmdVideoOutputDualStream3D;
    }

    BMDDisplayModeSupport support;

    if( _deckLinkOutput->DoesSupportVideoMode( desiredMode, 0, outputFlags, &support, &displayMode ) != S_OK) {
    LOG(WARNING) << "Unable to find a supported output mode";
    return false;
  }

    if( support == bmdDisplayModeNotSupported ) {
LOG(WARNING) << "Display mode not supported";
  return false;
}


    // Enable video output
    result = _deckLinkOutput->EnableVideoOutput(desiredMode, outputFlags);
    if(result != S_OK)
    {
      LOGF(WARNING, "Could not enable video output - result = %08x\n", result);
      return false;
    }

    // Set the callback object to the DeckLink device's output interface
    _outputCallback = new OutputCallback( _deckLinkOutput, displayMode );
    result = _deckLinkOutput->SetScheduledFrameCompletionCallback( _outputCallback );
    if(result != S_OK)
    {
      LOGF(WARNING, "Could not set callback - result = %08x\n", result);
      return false;
    }

    displayMode->Release();


    return true;
  }


  //=================================================================


  // Thread entry point
  void DeckLinkSource::operator()() {
    initialize();

    if( _initialized ) {
      // startStreams();
      doneSync.wait();
      stopStreams();
    }
  }

  bool DeckLinkSource::initialize()
  {
    _initialized = false;

    if( !_deckLink && !setDeckLink() ) {
        LOG(FATAL) << "Error creating Decklink card";
        return false;
    }
    CHECK( (bool)_deckLink ) << "_deckLink not set";

    if( !_deckLinkOutput && !createVideoOutput() ) {
        LOG(FATAL) << "Error creating video output";
        return false;
    }
    CHECK( (bool)_deckLinkOutput ) << "_deckLinkOutput not set";

    if( !_deckLinkInput && !createVideoInput() ) {
        LOG(FATAL) << "Error creating video input";
        return false;
    }
    CHECK( (bool)_deckLinkInput ) << "_deckLinkInput not set";

    _initialized = true;
    initializedSync.notify();

    return _initialized;
  }

  void DeckLinkSource::startStreams( void )
  {
    //
    //  result = g_deckLinkInput->EnableAudioInput(bmdAudioSampleRate48kHz, g_config.m_audioSampleDepth, g_config.m_audioChannels);
    //  if (result != S_OK)
    //          goto bail;
    //

    CHECK( _deckLink != nullptr );

    auto result = _deckLinkInput->StartStreams();
    if (result != S_OK) {
      LOG(WARNING) << "Failed to start streams";
      return;
    }

  }

  void DeckLinkSource::stopStreams( void )
  {
    CHECK( _deckLinkInput != nullptr );
    auto result = _deckLinkInput->StopStreams();
    if (result != S_OK) {
      LOG(WARNING) << "Failed to stop streams";
      return;
    }
  }

  bool DeckLinkSource::grab( void )
  {
    if( _inputCallback ) {

      while( _inputCallback->queue().try_and_pop(_grabbedImage) ) {;}

      if( !_grabbedImage.empty() ) return true;

      // If there was nothing in the queue, wait
      if( _inputCallback->queue().wait_for_pop(_grabbedImage, std::chrono::milliseconds(100) ) == false ) {
        LOG(WARNING) << "Timeout waiting for image in image queue";
        return false;
      }

      return true;
    }

    return false;
  }

  bool DeckLinkSource::sendSDICameraControl()
  {
    if( !_deckLinkOutput ) {
      if (!createVideoOutput()) return false;
    }

    CHECK( _deckLinkOutput != nullptr );

    // IDeckLinkMutableVideoFrame *videoFrameBlue = CreateSDICameraControlFrame(_deckLinkOutput);
    //
    // // These are magic values for 1080i50   See SDK manual page 213
    // const uint32_t kFrameDuration = 1000;
    // const uint32_t kTimeScale = 25000;
    //
    // auto result = _deckLinkOutput->ScheduleVideoFrame(videoFrameBlue, 0, kFrameDuration, kTimeScale);
    // if(result != S_OK)
    // {
    //   LOG(WARNING) << "Could not schedule video frame - result = " << std::hex << result;
    //   return false;
    // }
    //
    // //
    // result = _deckLinkOutput->StartScheduledPlayback(0, kTimeScale, 1.0);
    // if(result != S_OK)
    // {
    //   LOG(WARNING) << "Could not start video playback - result = " << std::hex << result;
    //   return false;
    // }
    //
    // // And stop after one frame
    // BMDTimeValue stopTime;
    // result = _deckLinkOutput->StopScheduledPlayback(kFrameDuration, &stopTime, kTimeScale);
    // if(result != S_OK)
    // {
    //   LOG(WARNING) << "Could not stop video playback - result = " << std::hex << result;
    //   return false;
    // }


    return true;
  }




  int DeckLinkSource::getImage( int i, cv::Mat &mat )
  {
    switch(i) {
    case 0:
        mat = _grabbedImage;
        return 1;
        break;
    default:
        return 0;
    }

    return 0;
  }

  ImageSize DeckLinkSource::imageSize( void ) const
  {
    return _inputCallback->imageSize();
  }

}
