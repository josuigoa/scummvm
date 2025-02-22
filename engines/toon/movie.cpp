/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/debug.h"
#include "common/events.h"
#include "common/keyboard.h"
#include "common/stream.h"
#include "common/system.h"
#include "graphics/palette.h"
#include "graphics/surface.h"

#include "toon/audio.h"
#include "toon/movie.h"
#include "toon/toon.h"
#include "toon/subtitles.h"

namespace Toon {

ToonstruckSmackerDecoder::ToonstruckSmackerDecoder() : Video::SmackerDecoder() {
	_lowRes = false;
}

void ToonstruckSmackerDecoder::handleAudioTrack(byte track, uint32 chunkSize, uint32 unpackedSize) {
	debugC(6, kDebugMovie, "handleAudioTrack(%d, %d, %d)", track, chunkSize, unpackedSize);

	if (track == 1 && chunkSize == 4) {
		/* uint16 width = */ _fileStream->readUint16LE();
		uint16 height = _fileStream->readUint16LE();
		_lowRes = (height == getHeight() / 2);
	} else {
		Video::SmackerDecoder::handleAudioTrack(track, chunkSize, unpackedSize);
	}
}

bool ToonstruckSmackerDecoder::loadStream(Common::SeekableReadStream *stream) {
	if (!Video::SmackerDecoder::loadStream(stream))
		return false;

	_lowRes = false;
	return true;
}

Video::SmackerDecoder::SmackerVideoTrack *ToonstruckSmackerDecoder::createVideoTrack(uint32 width, uint32 height, uint32 frameCount, const Common::Rational &frameRate, uint32 flags, uint32 version) const {
	return Video::SmackerDecoder::createVideoTrack(width, height, frameCount, frameRate, (height == 200) ? 4 : flags, version);
}

// decoder is deallocated with Movie destruction i.e. new ToonstruckSmackerDecoder is needed
Movie::Movie(ToonEngine *vm , ToonstruckSmackerDecoder *decoder) {
	_vm = vm;
	_playing = false;
	_decoder = decoder;
	_subtitle = new SubtitleRenderer(_vm);
}

Movie::~Movie() {
	delete _decoder;
	delete _subtitle;
}

void Movie::init() const {
}

void Movie::play(const Common::Path &video, int32 flags) {
	debugC(1, kDebugMovie, "play(%s, %d)", video.toString().c_str(), flags);
	bool isFirstIntroVideo = false;
	if (video == "209_1M.SMK")
		isFirstIntroVideo = true;

	_playing = true;
	if (flags & 1)
		_vm->getAudioManager()->setMusicVolume(0);
	if (!_decoder->loadFile(video)) {
		if (flags & 2)
			return;
		error("Unable to play video %s", video.toString().c_str());
	}
	_subtitle->load(video);
	playVideo(isFirstIntroVideo);
	_vm->flushPalette(true);
	if (flags & 1)
		_vm->getAudioManager()->setMusicVolume(_vm->getAudioManager()->isMusicMuted() ? 0 : 255);
	_decoder->close();
	_playing = false;
}

void Movie::playVideo(bool isFirstIntroVideo) {
	debugC(1, kDebugMovie, "playVideo(isFirstIntroVideo: %d)", isFirstIntroVideo);

	_decoder->start();

	while (!_vm->shouldQuit() && !_decoder->endOfVideo()) {
		if (_decoder->needsUpdate()) {
			const Graphics::Surface *frame = _decoder->decodeNextFrame();
			byte unused = 0;
			if (frame) {
				if (_decoder->isLowRes()) {
					// handle manually 2x scaling here
					Graphics::Surface* surf = _vm->_system->lockScreen();
					for (int y = 0; y < frame->h / 2; y++) {
						memcpy(surf->getBasePtr(0, y * 2 + 0), frame->getBasePtr(0, y), frame->pitch);
						memcpy(surf->getBasePtr(0, y * 2 + 1), frame->getBasePtr(0, y), frame->pitch);
					}
					_vm->_system->unlockScreen();
				} else {
					_vm->_system->copyRectToScreen(frame->getPixels(), frame->pitch, 0, 0, frame->w, frame->h);

					int32 currentFrame = _decoder->getCurFrame();

					// find unused color key to replace with subtitles color
					uint len = frame->w * frame->h;
					const byte *pixels = (const byte *)frame->getPixels();
					bool counts[256];
					memset(counts, false, sizeof(counts));
					for (uint i = 0; i < len; i++) {
						counts[pixels[i]] = true;
					}

					// 0 is already used for the border color and should not be used here, so it can be skipped over.
					for (int j = 1; j < 256; j++) {
						if (!counts[j]) {
							unused = j;
							break;
						}
					}

					_subtitle->render(*frame, currentFrame, unused);

					// WORKAROUND: There is an encoding glitch in the first intro video. This hides this using the adjacent pixels.
					if (isFirstIntroVideo) {
						// int32 currentFrame = _decoder->getCurFrame();
						if (currentFrame >= 956 && currentFrame <= 1038) {
							debugC(1, kDebugMovie, "Triggered workaround for glitch in first intro video...");
							_vm->_system->copyRectToScreen(frame->getBasePtr(frame->w-188, 123), frame->pitch, frame->w-188, 124, 188, 1);
							_vm->_system->copyRectToScreen(frame->getBasePtr(frame->w-188, 126), frame->pitch, frame->w-188, 125, 188, 1);
							_vm->_system->copyRectToScreen(frame->getBasePtr(0, 125), frame->pitch, 0, 126, 64, 1);
							_vm->_system->copyRectToScreen(frame->getBasePtr(0, 128), frame->pitch, 0, 127, 64, 1);
						}
					}
				}
			}

			byte subtitleColor[3] = {0xff, 0xff, 0x0};
			_vm->_system->getPaletteManager()->setPalette(_decoder->getPalette(), 0, 256);
			if (unused) {
				_vm->_system->getPaletteManager()->setPalette(subtitleColor, unused, 1);
			}
			_vm->_system->updateScreen();
		}

		Common::Event event;
		while (_vm->_system->getEventManager()->pollEvent(event))
			if ((event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE)) {
				_vm->dirtyAllScreen();
				return;
			}

		_vm->_system->delayMillis(10);
	}
	_vm->dirtyAllScreen();
	return;
}

} // End of namespace Toon
