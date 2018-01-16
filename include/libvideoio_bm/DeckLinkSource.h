#pragma once

#include <atomic>


#include <DeckLinkAPI.h>

#include "libvideoio/DataSource.h"

#include "InputCallback.h"
#include "OutputCallback.h"
#include "SDICameraControl.h"


namespace libvideoio_bm {

  using libvideoio::DataSource;
  using libvideoio::ImageSize;


class DeckLinkSource : public libvideoio::DataSource {
public:

	DeckLinkSource();
  ~DeckLinkSource();

  // Thread entry point
  void operator()();

  void initialize();
  bool initialized() const { return _initialized; }

  virtual int numFrames( void ) const { return -1; }

  bool createVideoOutput();
  bool sendSDICameraControl();

  // // Delete copy operators
  // DeckLinkSource( const DeckLinkSource & ) = delete;
  // DeckLinkSource &operator=( const DeckLinkSource & ) = delete;

  virtual bool grab( void );

  virtual int getImage( int i, cv::Mat &mat );

  virtual ImageSize imageSize( void ) const;

  // These start and stop the input streams
  void startStreams();
  void stopStreams();

  ThreadSynchronizer doneSync;
  ThreadSynchronizer initializedSync;

protected:

  bool findDeckLink();

  cv::Mat _grabbedImage;

  bool _initialized;

  // For now assume an object uses just one Decklink board
  // Stupid COM model precludes use of auto ptrs
  IDeckLink *_deckLink;
  IDeckLinkInput *_deckLinkInput;
  IDeckLinkOutput *_deckLinkOutput;

  InputCallback *_inputCallback;
  OutputCallback *_outputCallback;


};

}
