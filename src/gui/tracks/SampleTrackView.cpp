/*
 * SampleTrackView.cpp
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
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

#include "SampleTrackView.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpacerItem>
#include <QVBoxLayout>

#include "BeatStudioRecorder.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

#include "ConfigManager.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "Engine.h"
#include "FadeButton.h"
#include "Mixer.h"
#include "MixerChannelLcdSpinBox.h"
#include "MixerView.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "SampleClip.h"
#include "SampleTrackWindow.h"
#include "TimePos.h"
#include "SongEditor.h"
#include "StringPairDrag.h"
#include "TrackContainerView.h"
#include "TrackLabelButton.h"


namespace lmms::gui
{


SampleTrackView::SampleTrackView( SampleTrack * _t, TrackContainerView* tcv ) :
	TrackView( _t, tcv )
{
	setFixedHeight( 32 );

	m_tlb = new TrackLabelButton(this, getTrackSettingsWidget());
	m_tlb->setCheckable(true);
	connect(m_tlb, SIGNAL(clicked(bool)),
			this, SLOT(showEffects()));
	m_tlb->setIcon(embed::getIconPixmap("sample_track"));
	m_tlb->show();

	m_mixerChannelNumber = new MixerChannelLcdSpinBox(2, getTrackSettingsWidget(), tr("Mixer channel"), this);
	m_mixerChannelNumber->show();

	m_volumeKnob = new VolumeKnob(KnobType::Small17, tr("VOL"), getTrackSettingsWidget(), Knob::LabelRendering::LegacyFixedFontSize, tr("Track volume"));
	m_volumeKnob->setModel( &_t->m_volumeModel );
	m_volumeKnob->setHintText( tr( "Channel volume:" ), "%" );
	m_volumeKnob->show();

	m_panningKnob = new Knob(KnobType::Small17, tr("PAN"), getTrackSettingsWidget(), Knob::LabelRendering::LegacyFixedFontSize, tr("Panning"));
	m_panningKnob->setModel( &_t->m_panningModel );
	m_panningKnob->setHintText( tr( "Panning:" ), "%" );
	m_panningKnob->show();

	m_activityIndicator = new FadeButton(
		QApplication::palette().color(QPalette::Active, QPalette::Window),
		QApplication::palette().color(QPalette::Active, QPalette::BrightText),
		QApplication::palette().color(QPalette::Active, QPalette::BrightText).darker(),
		getTrackSettingsWidget()
	);
	m_activityIndicator->setFixedSize(8, 28);
	m_activityIndicator->show();

	auto masterLayout = new QVBoxLayout(getTrackSettingsWidget());
	masterLayout->setContentsMargins(0, 1, 0, 0);
	auto layout = new QHBoxLayout();
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_tlb);
	layout->addWidget(m_mixerChannelNumber);
	layout->addWidget(m_activityIndicator);
	layout->addWidget(m_volumeKnob);
	layout->addWidget(m_panningKnob);

	// Beat Studio: Add record button to sample track
	m_recordButton = new QPushButton(getTrackSettingsWidget());
	m_recordButton->setIcon(embed::getIconPixmap("record"));
	m_recordButton->setToolTip(tr("Arm track for recording"));
	m_recordButton->setCheckable(true);
	m_recordButton->setFixedSize(14, 14);
	m_recordButton->setStyleSheet(
		"QPushButton { background: #2a2a2a; border: 1px solid #444; border-radius: 3px; }"
		"QPushButton:checked { background: #cc0000; border: 1px solid #ff0000; }"
		"QPushButton:hover { border: 1px solid #ff8c00; }"
	);
	// Beat Studio: standalone recorder
	m_recorder = new BeatStudioRecorder(this);
	connect(m_recorder, &BeatStudioRecorder::recordingFinished, this, [](const QString& filePath) {
		qDebug("[BeatStudio] Recording saved to: %s", qPrintable(filePath));
	}, Qt::QueuedConnection);
	connect(m_recordButton, &QPushButton::toggled, [this](bool checked) {
		if (checked) {
			m_recorder->startRecording();
		} else {
			m_recorder->stopRecording();
		}
	});
	layout->addWidget(m_recordButton);

	masterLayout->addLayout(layout);
	masterLayout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

	connect(_t, SIGNAL(playingChanged()), this, SLOT(updateIndicator()));

	setModel( _t );

	m_window = new SampleTrackWindow(this);
	m_window->toggleVisibility(false);
}




void SampleTrackView::updateIndicator()
{
	if (model()->isPlaying()) { m_activityIndicator->activateOnce(); }
	else { m_activityIndicator->noteEnd(); }
}




SampleTrackView::~SampleTrackView()
{
	if(m_window != nullptr)
	{
		m_window->setSampleTrackView(nullptr);
		m_window->parentWidget()->hide();
	}
	m_window = nullptr;
}



//FIXME: This is identical to InstrumentTrackView::createMixerMenu
QMenu * SampleTrackView::createMixerMenu(QString title, QString newMixerLabel)
{
	int channelIndex = model()->mixerChannelModel()->value();

	MixerChannel *mixerChannel = Engine::mixer()->mixerChannel(channelIndex);

	// If title allows interpolation, pass channel index and name
	if (title.contains("%2"))
	{
		title = title.arg(channelIndex).arg(mixerChannel->m_name);
	}

	auto mixerMenu = new QMenu(title);

	mixerMenu->addAction(newMixerLabel, this, SLOT(createMixerLine()));
	mixerMenu->addSeparator();

	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		MixerChannel * currentChannel = Engine::mixer()->mixerChannel(i);

		if (currentChannel != mixerChannel)
		{
			const auto index = currentChannel->index();
			QString label = tr("%1: %2").arg(index).arg(currentChannel->m_name);
			mixerMenu->addAction(label, [this, index](){
				assignMixerLine(index);
			});
		}
	}

	return mixerMenu;
}




void SampleTrackView::showEffects()
{
	m_window->toggleVisibility(m_window->parentWidget()->isHidden());
}



void SampleTrackView::modelChanged()
{
	auto st = castModel<SampleTrack>();
	m_volumeKnob->setModel(&st->m_volumeModel);
	m_mixerChannelNumber->setModel(&st->m_mixerChannelModel);

	TrackView::modelChanged();
}




void SampleTrackView::dragEnterEvent(QDragEnterEvent *dee)
{
	// Accept both internal samplefile drags and files from Explorer
	if (dee->mimeData()->hasUrls()) {
		dee->acceptProposedAction();
		return;
	}
	StringPairDrag::processDragEnterEvent(dee, QString("samplefile"));
}




void SampleTrackView::dropEvent(QDropEvent *de)
{
	// Handle files dragged from Windows Explorer
	if (de->mimeData()->hasUrls()) {
		for (const QUrl& url : de->mimeData()->urls()) {
			QString filePath = url.toLocalFile();
			if (filePath.isEmpty()) continue;
			qDebug("[BeatStudio] Explorer drop: %s", qPrintable(filePath));
			auto sClip = static_cast<SampleClip*>(getTrack()->createClip(TimePos(0)));
			if (sClip) {
					sClip->setSampleFile(filePath);
				qDebug("[BeatStudio] Explorer drop: done");
			}
			break; // only first file
		}
		de->acceptProposedAction();
		return;
	}

	QString type  = StringPairDrag::decodeKey(de);
	QString value = StringPairDrag::decodeValue(de);

	if (type == "samplefile")
	{
		int trackHeadWidth = ConfigManager::inst()->value("ui", "compacttrackbuttons").toInt()==1
				? DEFAULT_SETTINGS_WIDGET_WIDTH_COMPACT + TRACK_OP_WIDTH_COMPACT
				: DEFAULT_SETTINGS_WIDGET_WIDTH + TRACK_OP_WIDTH;

		const int deX = position(de).x();
		int xPos = deX < trackHeadWidth
				? trackHeadWidth
				: deX;

		const float snapSize = getGUI()->songEditor()->m_editor->getSnapSize();
		TimePos clipPos = trackContainerView()->fixedClips()
				? TimePos(0)
				: TimePos(((xPos - trackHeadWidth) / trackContainerView()->pixelsPerBar()
							* TimePos::ticksPerBar()) + trackContainerView()->currentPosition()
						).quantize(snapSize, true);

		auto sClip = static_cast<SampleClip*>(getTrack()->createClip(clipPos));
		if (sClip) {
			qDebug("[BeatStudio] dropEvent: addClip");
			qDebug("[BeatStudio] dropEvent: setSampleFile %s", qPrintable(value));
			sClip->setSampleFile(value);
			qDebug("[BeatStudio] dropEvent: done");
		}
	}
}




/*! \brief Create and assign a new mixer Channel for this track */
void SampleTrackView::createMixerLine()
{
	int channelIndex = getGUI()->mixerView()->addNewChannel();
	auto channel = Engine::mixer()->mixerChannel(channelIndex);

	channel->m_name = getTrack()->name();
	channel->setColor(getTrack()->color());

	assignMixerLine(channelIndex);
}




/*! \brief Assign a specific mixer Channel for this track */
void SampleTrackView::assignMixerLine(int channelIndex)
{
	model()->mixerChannelModel()->setValue(channelIndex);

	getGUI()->mixerView()->setCurrentMixerChannel(channelIndex);
}


} // namespace lmms::gui
