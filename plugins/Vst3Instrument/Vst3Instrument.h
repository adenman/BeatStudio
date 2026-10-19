/*
 * Vst3Instrument.h - hosts VST3 instruments
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

#ifndef LMMS_VST3_INSTRUMENT_H
#define LMMS_VST3_INSTRUMENT_H

#include "Instrument.h"
#include "InstrumentView.h"
#include "Vst3Host.h"

namespace lmms
{

namespace gui
{
class Vst3InstrumentView;
class Vst3PluginPanel;
}

class Vst3Instrument : public Instrument
{
	Q_OBJECT
public:
	Vst3Instrument(InstrumentTrack* track, Descriptor::SubPluginFeatures::Key* key);
	~Vst3Instrument() override;

	void play(SampleFrame* workingBuffer) override;
	bool handleMidiEvent(const MidiEvent& event, const TimePos& time = TimePos(), f_cnt_t offset = 0) override;

	void saveSettings(QDomDocument& doc, QDomElement& elem) override;
	void loadSettings(const QDomElement& elem) override;
	void loadFile(const QString& file) override;
	QString nodeName() const override;

	gui::PluginView* instantiateView(QWidget* parent) override;

	//! Loads a module (GUI thread). Returns an error message, or an empty string on success.
	QString loadModule(const QString& path, const QString& classId = QString());

	Vst3Host& host() { return m_host; }

signals:
	void pluginChanged(const QString& error);

private:
	void setupProcessing();
	void updateDisplayName();

	Vst3Host m_host;
	QString m_lastError;
	bool m_settingsLoaded = false;

	friend class gui::Vst3InstrumentView;
};


namespace gui
{

class Vst3InstrumentView : public InstrumentView
{
	Q_OBJECT
public:
	Vst3InstrumentView(Vst3Instrument* instrument, QWidget* parent);

protected:
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;

private:
	void modelChanged() override;

	Vst3PluginPanel* m_panel;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_VST3_INSTRUMENT_H
