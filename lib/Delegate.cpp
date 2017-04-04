

#include "libvideoio_bm/Delegate.h"

namespace libvideoio_bm {

  DeckLinkCaptureDelegate::DeckLinkCaptureDelegate() : m_refCount(1)
  {
  }

  ULONG DeckLinkCaptureDelegate::AddRef(void)
  {
    return __sync_add_and_fetch(&m_refCount, 1);
  }

  ULONG DeckLinkCaptureDelegate::Release(void)
  {
    int32_t newRefValue = __sync_sub_and_fetch(&m_refCount, 1);
    if (newRefValue == 0)
    {
      delete this;
      return 0;
    }
    return newRefValue;
  }

  HRESULT DeckLinkCaptureDelegate::VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket* audioFrame)
  {
    IDeckLinkVideoFrame *rightEyeFrame = NULL;
    IDeckLinkVideoFrame3DExtensions *threeDExtensions = NULL;
    void *frameBytes;
    void *audioFrameBytes;

    // Handle Video Frame
    if (videoFrame)
    {
      // If 3D mode is enabled we retreive the 3D extensions interface which gives.
      // us access to the right eye frame by calling GetFrameForRightEye() .
      if ( (videoFrame->QueryInterface(IID_IDeckLinkVideoFrame3DExtensions, (void **) &threeDExtensions) != S_OK) ||
      (threeDExtensions->GetFrameForRightEye(&rightEyeFrame) != S_OK))
      {
        rightEyeFrame = NULL;
      }

      if (threeDExtensions)
      threeDExtensions->Release();

      if (videoFrame->GetFlags() & bmdFrameHasNoInputSource)
      {
        printf("Frame received (#%lu) - No input signal detected\n", g_frameCount);
      }
      else
      {
        const char *timecodeString = NULL;
        if (g_config.m_timecodeFormat != 0)
        {
          IDeckLinkTimecode *timecode;
          if (videoFrame->GetTimecode(g_config.m_timecodeFormat, &timecode) == S_OK)
          {
            timecode->GetString(&timecodeString);
          }
        }

        printf("Frame received (#%lu) [%s] - %s - Size: %li bytes\n",
        g_frameCount,
        timecodeString != NULL ? timecodeString : "No timecode",
        rightEyeFrame != NULL ? "Valid Frame (3D left/right)" : "Valid Frame",
        videoFrame->GetRowBytes() * videoFrame->GetHeight());

        if (timecodeString)
        free((void*)timecodeString);

        if (g_videoOutputFile != -1)
        {
          videoFrame->GetBytes(&frameBytes);
          write(g_videoOutputFile, frameBytes, videoFrame->GetRowBytes() * videoFrame->GetHeight());

          if (rightEyeFrame)
          {
            rightEyeFrame->GetBytes(&frameBytes);
            write(g_videoOutputFile, frameBytes, videoFrame->GetRowBytes() * videoFrame->GetHeight());
          }
        }
      }

      if (rightEyeFrame)
      rightEyeFrame->Release();
      g_frameCount++;
    }

    // Handle Audio Frame
    if (audioFrame)
    {
      if (g_audioOutputFile != -1)
      {
        audioFrame->GetBytes(&audioFrameBytes);
        write(g_audioOutputFile, audioFrameBytes, audioFrame->GetSampleFrameCount() * g_config.m_audioChannels * (g_config.m_audioSampleDepth / 8));
      }
    }

    if (g_config.m_maxFrames > 0 && videoFrame && g_frameCount >= g_config.m_maxFrames)
    {
      g_do_exit = true;
      pthread_cond_signal(&g_sleepCond);
    }

    return S_OK;
  }

  HRESULT DeckLinkCaptureDelegate::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode *mode, BMDDetectedVideoInputFormatFlags formatFlags)
  {
    // This only gets called if bmdVideoInputEnableFormatDetection was set
    // when enabling video input
    HRESULT result;
    char*   displayModeName = NULL;
    BMDPixelFormat  pixelFormat = bmdFormat10BitYUV;

    if (formatFlags & bmdDetectedVideoInputRGB444)
    pixelFormat = bmdFormat10BitRGB;

    mode->GetName((const char**)&displayModeName);
    printf("Video format changed to %s %s\n", displayModeName, formatFlags & bmdDetectedVideoInputRGB444 ? "RGB" : "YUV");

    if (displayModeName)
    free(displayModeName);

    if (g_deckLinkInput)
    {
      g_deckLinkInput->StopStreams();

      result = g_deckLinkInput->EnableVideoInput(mode->GetDisplayMode(), pixelFormat, g_config.m_inputFlags);
      if (result != S_OK)
      {
        fprintf(stderr, "Failed to switch video mode\n");
        goto bail;
      }

      g_deckLinkInput->StartStreams();
    }
    bail:
    return S_OK;
  }
}