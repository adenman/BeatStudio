/*
 * Vst3Host.h - in-process host for a single VST3 plugin instance
 *
 * This file is part of BeatStudio (based on LMMS - https://lmms.io)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_VST3_HOST_H
#define LMMS_VST3_HOST_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "LmmsTypes.h"

class QDomDocument;
class QDomElement;
class QWidget;

namespace lmms
{

class MidiEvent;
class SampleFrame;

namespace detail { struct Vst3HostImpl; }

/**
 * Hosts one VST3 audio processor class (instrument or effect).
 *
 * Threading model:
 *  - load()/unload()/saveSettings()/loadSettings() run on the GUI thread, wrapped by the
 *    caller in AudioEngine::requestChangeInModel()/doneChangeInModel()
 *  - process() runs on the audio thread
 *  - queueMidi() and parameter edits coming from the plugin editor may run on any thread;
 *    they are handed to the audio thread through a mutex-protected queue
 */
class Vst3Host
{
public:
	enum class Kind { Instrument, Effect };

	explicit Vst3Host(Kind kind);
	~Vst3Host();

	Vst3Host(const Vst3Host&) = delete;
	Vst3Host& operator=(const Vst3Host&) = delete;

	//! Lists every .vst3 module in the platform's standard VST3 folders
	static QStringList installedModules();

	//! Loads a .vst3 module (file or bundle folder). If @p classId is empty, the first class
	//! matching this host's kind is used. Returns false and fills @p error on failure.
	bool load(const QString& modulePath, const QString& classId, QString& error);
	void unload();
	bool isLoaded() const;

	QString modulePath() const;
	QString classId() const;
	QString name() const;
	QString vendor() const;

	//! (Re)configures processing; call after loading and whenever sample rate or period changes
	void setupProcessing(sample_rate_t sampleRate, f_cnt_t maxFrames);

	//! Audio thread. @p in may be null (instruments). @p out receives the main stereo output.
	//! Returns false if nothing was processed (no plugin, or the plugin failed).
	bool process(const SampleFrame* in, SampleFrame* out, f_cnt_t frames);

	//! Any thread. Converts an LMMS MIDI event to VST3 events / parameter changes.
	void queueMidi(const MidiEvent& event, f_cnt_t offset);
	void allNotesOff();

	//! Plugin state (component + controller), stored as base64 in the project file
	void saveSettings(QDomDocument& doc, QDomElement& elem) const;
	//! Reads module path + state; loads the module. Returns false and fills @p error on failure.
	bool loadSettings(const QDomElement& elem, QString& error);

	bool hasEditor() const;
	//! Shows the plugin's own editor in a top-level window (created on first use)
	void showEditor(QWidget* parentForPosition = nullptr);
	void hideEditor();
	bool isEditorVisible() const;
	//! Called when the editor window is closed by the user
	std::function<void()> onEditorClosed;

private:
	std::unique_ptr<detail::Vst3HostImpl> m_impl;
};

} // namespace lmms

#endif // LMMS_VST3_HOST_H
