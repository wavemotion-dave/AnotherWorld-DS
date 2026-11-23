/* Raw - Another World Interpreter
 * Copyright (C) 2004 Gregory Montoir
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "mixer.h"
#include "serializer.h"
#include "systemstub.h"

// Per-channel gain applied on top of the channel volume (0..63).
// 2 => a single full-volume channel reaches half of full scale, which leaves
// headroom for several channels. Raise to 3 or 4 if the output is too quiet.
#define MIX_GAIN 3

// Largest block handled in one pass (accumulator - on stack so can't be too large)
#define MIX_MAX_BLOCK 512

Mixer::Mixer(SystemStub *stub) 
	: _stub(stub) {
}

void Mixer::init() {
	memset(_channels, 0, sizeof(_channels));
	_mutex = _stub->createMutex();
	_stub->startAudio(Mixer::mixCallback, this);
}

void Mixer::free() {
	stopAll();
	_stub->stopAudio();
	_stub->destroyMutex(_mutex);
}

void Mixer::playChannel(uint8 channel, const MixerChunk *mc, uint16 freq, uint8 volume) {
	debug(DBG_SND, "Mixer::playChannel(%d, %d, %d)", channel, freq, volume);
	assert(channel < NUM_CHANNELS);
	MutexStack(_stub, _mutex);
	MixerChannel *ch = &_channels[channel];
	uint32 rate = _stub->getOutputSampleRate();
	ch->active = true;
	ch->volume = volume;
	ch->chunk = *mc;
	ch->chunkPos = 0;
	// 24.8 fixed point, rounded to nearest to reduce pitch error
	ch->chunkInc = (((uint32)freq << 8) + (rate >> 1)) / rate;
}

void Mixer::stopChannel(uint8 channel) {
	debug(DBG_SND, "Mixer::stopChannel(%d)", channel);
	assert(channel < NUM_CHANNELS);
	MutexStack(_stub, _mutex);	
	_channels[channel].active = false;
}

void Mixer::setChannelVolume(uint8 channel, uint8 volume) {
	debug(DBG_SND, "Mixer::setChannelVolume(%d, %d)", channel, volume);
	assert(channel < NUM_CHANNELS);
	MutexStack(_stub, _mutex);
	_channels[channel].volume = volume;
}

void Mixer::stopAll() {
	debug(DBG_SND, "Mixer::stopAll()");
	MutexStack(_stub, _mutex);
	for (uint8 i = 0; i < NUM_CHANNELS; ++i) {
		_channels[i].active = false;		
	}
}

// Mixes up to MIX_MAX_BLOCK samples. Channel-major loop: cheaper than the
// per-sample channel loop and keeps each channel's state in registers.
ITCM_CODE static void mixBlock(Mixer *m, s16 *buf, int len) 
{
	int32 acc[MIX_MAX_BLOCK];

	memset(acc, 0, len * sizeof(int32));

	for (int i = 0; i < Mixer::NUM_CHANNELS; ++i) {
		MixerChannel *ch = &m->_channels[i];
		if (!ch->active) {
			continue;
		}

		const int8 *data = (const int8 *)ch->chunk.data;
		const bool loop = (ch->chunk.loopLen != 0);
		const uint32 loopLen = ch->chunk.loopLen;
		const uint32 loopPos = ch->chunk.loopPos;
		const uint32 end = loop ? (loopPos + loopLen) : (uint32)ch->chunk.len;

		if (data == 0 || end == 0) {
			ch->active = false;
			continue;
		}

		const int vol = ch->volume * MIX_GAIN;
		const uint32 inc = ch->chunkInc;
		uint32 pos = ch->chunkPos;

		for (int j = 0; j < len; ++j) {
			if (loop) {
				// wrap keeping the fractional phase (no click at the loop point)
				while (pos >= (end << 8)) {
					pos -= loopLen << 8;
				}
			} else if ((pos >> 8) >= end - 1) {
				ch->active = false;
				break;
			}

			uint32 p1 = pos >> 8;
			uint32 frac = pos & 0xFF;
			uint32 p2 = p1 + 1;
			if (loop && p2 >= end) {
				p2 = loopPos;
			}

			int sample = ((int)data[p1] * (int)(256 - frac) + (int)data[p2] * (int)frac) >> 8;
			acc[j] += sample * vol;
            pos += inc;
		}

		ch->chunkPos = pos;
	}

	for (int j = 0; j < len; ++j) {
		int32 v = acc[j];
		if (v > 32767) {
			v = 32767;
		} else if (v < -32768) {
			v = -32768;
		}
		buf[j] = (s16)v;
	}
}

ITCM_CODE void Mixer::mix(s16 *buf, int len) {
	MutexStack(_stub, _mutex);
	while (len > 0) {
		int n = (len > MIX_MAX_BLOCK) ? MIX_MAX_BLOCK : len;
		mixBlock(this, (s16*)memUncached(buf), n);
		buf += n;
		len -= n;
	}
}

ITCM_CODE void Mixer::mixCallback(void *param, u16 *buf, int len) {
	((Mixer *)param)->mix((s16*)buf, len);
}

void Mixer::saveOrLoad(Serializer &ser) {
	_stub->lockMutex(_mutex);
	for (int i = 0; i < NUM_CHANNELS; ++i) {
		MixerChannel *ch = &_channels[i];
		Serializer::Entry entries[] = {
			SE_INT(&ch->active, Serializer::SES_BOOL, VER(2)),
			SE_INT(&ch->volume, Serializer::SES_INT8, VER(2)),
			SE_INT(&ch->chunkPos, Serializer::SES_INT32, VER(2)),
			SE_INT(&ch->chunkInc, Serializer::SES_INT32, VER(2)),
			SE_PTR(&ch->chunk.data, VER(2)),
			SE_INT(&ch->chunk.len, Serializer::SES_INT16, VER(2)),
			SE_INT(&ch->chunk.loopPos, Serializer::SES_INT16, VER(2)),
			SE_INT(&ch->chunk.loopLen, Serializer::SES_INT16, VER(2)),
			SE_END()
		};
		ser.saveOrLoadEntries(entries);
	}
	_stub->unlockMutex(_mutex);
};
