/*
 * Vst3PluginPanel.h - plugin picker + editor toggle shared by the VST3 instrument and effect
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

#ifndef LMMS_GUI_VST3_PLUGIN_PANEL_H
#define LMMS_GUI_VST3_PLUGIN_PANEL_H

#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QPushButton;

namespace lmms
{

class Vst3Host;

namespace gui
{

class Vst3PluginPanel : public QWidget
{
public:
	//! @p loadModule is called with the module path the user picked; it returns an error
	//! message, or an empty string on success
	Vst3PluginPanel(Vst3Host& host, std::function<QString(const QString&)> loadModule, QWidget* parent);
	~Vst3PluginPanel() override;

	//! Re-reads the host state into the labels and buttons
	void refresh(const QString& error = QString());

private:
	void pickModule(const QString& path);
	void browse();

	Vst3Host& m_host;
	std::function<QString(const QString&)> m_loadModule;

	QLabel* m_nameLabel;
	QLabel* m_infoLabel;
	QComboBox* m_modules;
	QPushButton* m_browseButton;
	QPushButton* m_editorButton;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_VST3_PLUGIN_PANEL_H
