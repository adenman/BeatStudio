/*
 * Vst3PluginPanel.cpp - plugin picker + editor toggle shared by the VST3 instrument and effect
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

#include "Vst3PluginPanel.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "FileDialog.h"
#include "lmmsconfig.h"
#include "Vst3Host.h"

namespace lmms::gui
{

Vst3PluginPanel::Vst3PluginPanel(Vst3Host& host, std::function<QString(const QString&)> loadModule, QWidget* parent) :
	QWidget(parent),
	m_host(host),
	m_loadModule(std::move(loadModule)),
	m_nameLabel(new QLabel(this)),
	m_infoLabel(new QLabel(this)),
	m_modules(new QComboBox(this)),
	m_browseButton(new QPushButton(tr("Browse..."), this)),
	m_editorButton(new QPushButton(tr("Show plugin window"), this))
{
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);

	auto nameFont = m_nameLabel->font();
	nameFont.setBold(true);
	nameFont.setPointSizeF(nameFont.pointSizeF() * 1.3);
	m_nameLabel->setFont(nameFont);
	m_nameLabel->setWordWrap(true);
	m_infoLabel->setWordWrap(true);
	m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

	m_modules->setToolTip(tr("VST3 plugins installed on this computer"));
	m_modules->addItem(tr("Choose a VST3 plugin..."));
	for (const auto& path : Vst3Host::installedModules())
	{
		m_modules->addItem(QFileInfo(path).completeBaseName(), path);
	}
	connect(m_modules, qOverload<int>(&QComboBox::activated), this, [this](int index) {
		const auto path = m_modules->itemData(index).toString();
		if (!path.isEmpty()) { pickModule(path); }
	});

	m_browseButton->setToolTip(tr("Load a .vst3 plugin from another folder"));
	connect(m_browseButton, &QPushButton::clicked, this, [this] { browse(); });

	m_editorButton->setCheckable(true);
	connect(m_editorButton, &QPushButton::toggled, this, [this](bool show) {
		if (show) { m_host.showEditor(this); }
		else { m_host.hideEditor(); }
		// the plugin may refuse to open its editor
		const QSignalBlocker blocker(m_editorButton);
		m_editorButton->setChecked(m_host.isEditorVisible());
	});
	m_host.onEditorClosed = [this] {
		const QSignalBlocker blocker(m_editorButton);
		m_editorButton->setChecked(false);
	};

	auto pickRow = new QHBoxLayout;
	pickRow->addWidget(m_modules, 1);
	pickRow->addWidget(m_browseButton);

	layout->addWidget(m_nameLabel);
	layout->addWidget(m_infoLabel);
	layout->addLayout(pickRow);
	layout->addWidget(m_editorButton);
	layout->addStretch(1);

	refresh();
}


Vst3PluginPanel::~Vst3PluginPanel()
{
	m_host.onEditorClosed = nullptr;
}


void Vst3PluginPanel::refresh(const QString& error)
{
	const bool loaded = m_host.isLoaded();

	m_nameLabel->setText(loaded ? m_host.name() : tr("No VST3 plugin loaded"));

	QString info;
	if (loaded && !m_host.vendor().isEmpty()) { info = m_host.vendor(); }
	if (!error.isEmpty())
	{
		info = QStringLiteral("<span style=\"color:#ff6060\">%1</span>").arg(error.toHtmlEscaped());
	}
	else if (!loaded)
	{
		info = tr("Pick a plugin below, or drag one from the plugin browser.");
	}
	m_infoLabel->setText(info);

	const int index = loaded ? m_modules->findData(m_host.modulePath()) : 0;
	m_modules->setCurrentIndex(std::max(index, 0));

	const QSignalBlocker blocker(m_editorButton);
	m_editorButton->setEnabled(loaded && m_host.hasEditor());
	m_editorButton->setChecked(m_host.isEditorVisible());
}


void Vst3PluginPanel::pickModule(const QString& path)
{
	if (path.isEmpty() || path == m_host.modulePath()) { return; }
	refresh(m_loadModule(path));
}


void Vst3PluginPanel::browse()
{
	FileDialog dialog(nullptr, tr("Open VST3 plugin"));
	dialog.setNameFilters({tr("VST3 plugins (*.vst3)"), tr("All files (*)")});

	if (m_host.isLoaded())
	{
		const QFileInfo current(m_host.modulePath());
		dialog.setDirectory(current.absolutePath());
		dialog.selectFile(current.fileName());
	}
	else
	{
#ifdef LMMS_BUILD_WIN32
		dialog.setDirectory(QDir::fromNativeSeparators(qEnvironmentVariable("CommonProgramFiles")) + "/VST3");
#else
		dialog.setDirectory(QDir::homePath() + "/.vst3");
#endif
	}

	if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
	{
		pickModule(QDir::toNativeSeparators(dialog.selectedFiles().first()));
	}
}

} // namespace lmms::gui
