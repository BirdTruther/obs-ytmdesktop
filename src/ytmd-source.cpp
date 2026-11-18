#include "ytmd-source.hpp"
#include "ytmd-client.hpp"

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr const char *kPropHost = "host";
constexpr const char *kPropPort = "port";
constexpr const char *kPropToken = "token";
constexpr const char *kPropPairBtn = "pair_button";
constexpr const char *kPropStatus = "pairing_status";
constexpr const char *kPropWidth = "width";
constexpr const char *kPropHeight = "height";
constexpr const char *kPropShowArt = "show_album_art";
constexpr const char *kPropShowProgress = "show_progress_bar";
constexpr const char *kPropHidePaused = "hide_when_paused";
constexpr const char *kPropBgColor = "bg_color";
constexpr const char *kPropBgOpacity = "bg_opacity";
constexpr const char *kPropAccentColor = "accent_color";
constexpr const char *kPropTextColor = "text_color";
constexpr const char *kPropSubTextColor = "subtext_color";
constexpr const char *kPropFontSize = "font_size";
constexpr const char *kPropCornerRadius = "corner_radius";

QColor obsColorToQColor(uint32_t abgr, int alphaPercent = 100)
{
	const int r = static_cast<int>(abgr & 0xFFu);
	const int g = static_cast<int>((abgr >> 8) & 0xFFu);
	const int b = static_cast<int>((abgr >> 16) & 0xFFu);
	const int a = std::clamp((alphaPercent * 255) / 100, 0, 255);
	return QColor(r, g, b, a);
}

QString formatTime(double seconds)
{
	if (seconds < 0.0) {
		seconds = 0.0;
	}
	const int total = static_cast<int>(std::floor(seconds));
	const int mins = total / 60;
	const int secs = total % 60;
	return QStringLiteral("%1:%2").arg(mins).arg(secs, 2, 10, QLatin1Char('0'));
}

struct YtmdSource {
	obs_source_t *source = nullptr;
	std::unique_ptr<YtmdClient> client;

	// Settings
	QString host = QStringLiteral("127.0.0.1");
	int port = 9863;
	QString token;
	uint32_t width = 520;
	uint32_t height = 136;
	bool showAlbumArt = true;
	bool showProgressBar = true;
	bool hideWhenPaused = false;
	uint32_t bgColor = 0x181818;
	int bgOpacity = 88;
	uint32_t accentColor = 0x3A3AFF; // Red-ish in ABGR -> FF3A3A
	uint32_t textColor = 0xFFFFFF;
	uint32_t subTextColor = 0xBBBBBB;
	int fontSize = 18;
	int cornerRadius = 14;

	// Rendering state
	gs_texture_t *texture = nullptr;
	uint32_t texWidth = 0;
	uint32_t texHeight = 0;
	QImage canvas;
	uint64_t settingsVersion = 1;
	uint64_t lastRenderedVersion = 0;
	uint64_t lastRenderedSettings = 0;
	int lastRenderedSecond = -1;
	int lastProgressPx = -1;
	bool visible = true;

	void renderCard(const YtmdSnapshot &snap, double elapsedSec)
	{
		const int w = static_cast<int>(width);
		const int h = static_cast<int>(height);
		if (w <= 0 || h <= 0) {
			return;
		}

		if (canvas.width() != w || canvas.height() != h) {
			canvas = QImage(w, h, QImage::Format_RGBA8888);
		}
		canvas.fill(Qt::transparent);

		QPainter p(&canvas);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setRenderHint(QPainter::SmoothPixmapTransform, true);
		p.setRenderHint(QPainter::TextAntialiasing, true);

		// Background card
		const QColor bg = obsColorToQColor(bgColor, bgOpacity);
		const qreal radius = static_cast<qreal>(std::min({cornerRadius, w / 2, h / 2}));
		QPainterPath bgPath;
		bgPath.addRoundedRect(QRectF(0, 0, w, h), radius, radius);
		p.fillPath(bgPath, bg);

		const int pad = std::max(10, h / 10);
		int textLeft = pad;

		// Album art
		if (showAlbumArt) {
			const int artSize = std::max(24, h - pad * 2);
			const QRectF artRect(pad, pad, artSize, artSize);
			const qreal artRadius = std::min(10.0, artSize * 0.15);

			p.save();
			QPainterPath artClip;
			artClip.addRoundedRect(artRect, artRadius, artRadius);
			p.setClipPath(artClip);

			if (!snap.albumArt.isNull()) {
				const QImage scaled = snap.albumArt.scaled(
					artSize, artSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
				const int sx = (scaled.width() - artSize) / 2;
				const int sy = (scaled.height() - artSize) / 2;
				p.drawImage(artRect, scaled, QRectF(sx, sy, artSize, artSize));
			} else {
				p.fillRect(artRect, QColor(45, 45, 45, 220));
				p.setPen(obsColorToQColor(subTextColor));
				QFont iconFont(QStringLiteral("Segoe UI"), std::max(12, artSize / 3));
				p.setFont(iconFont);
				p.drawText(artRect, Qt::AlignCenter, QStringLiteral("♫"));
			}
			p.restore();
			textLeft = pad + artSize + pad;
		}

		const int textRight = w - pad;
		const int textWidth = std::max(40, textRight - textLeft);

		// Title & Artist
		const QString title = snap.track.valid
			? snap.track.title
			: (snap.statusText.isEmpty() ? QStringLiteral("Waiting for YTMDesktop...") : snap.statusText);
		QString subtitle;
		if (snap.track.valid) {
			subtitle = snap.track.author;
			if (!snap.track.album.isEmpty()) {
				if (!subtitle.isEmpty()) {
					subtitle += QStringLiteral(" • ");
				}
				subtitle += snap.track.album;
			}
		} else {
			subtitle = QStringLiteral("Open YTMDesktop & enable Companion Server");
		}

		QFont titleFont(QStringLiteral("Segoe UI"), fontSize, QFont::DemiBold);
		QFont subFont(QStringLiteral("Segoe UI"), std::max(9, fontSize - 4), QFont::Normal);
		QFont timeFont(QStringLiteral("Segoe UI"), std::max(8, fontSize - 6), QFont::Normal);

		const QFontMetrics titleFm(titleFont);
		const QFontMetrics subFm(subFont);

		const int barAreaHeight = showProgressBar ? 26 : 0;
		const int contentTop = pad;
		const int contentHeight = std::max(20, h - pad * 2 - barAreaHeight);

		const int titleY = contentTop + std::max(0, (contentHeight - (titleFm.height() + subFm.height() + 4)) / 2);

		p.setFont(titleFont);
		p.setPen(obsColorToQColor(textColor));
		const QString elidedTitle = titleFm.elidedText(title, Qt::ElideRight, textWidth);
		p.drawText(QRect(textLeft, titleY, textWidth, titleFm.height()), Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);

		p.setFont(subFont);
		p.setPen(obsColorToQColor(subTextColor));
		const QString elidedSub = subFm.elidedText(subtitle, Qt::ElideRight, textWidth);
		p.drawText(QRect(textLeft, titleY + titleFm.height() + 2, textWidth, subFm.height()),
			   Qt::AlignLeft | Qt::AlignVCenter, elidedSub);

		// Progress bar
		if (showProgressBar) {
			const int barHeight = 5;
			const int barY = h - pad - barHeight;
			const double duration = snap.track.durationSeconds;
			const double ratio = (snap.track.valid && duration > 0.0)
				? std::clamp(elapsedSec / duration, 0.0, 1.0)
				: 0.0;

			p.setFont(timeFont);
			p.setPen(obsColorToQColor(subTextColor));
			const QString timeStr = snap.track.valid
				? QStringLiteral("%1 / %2").arg(formatTime(elapsedSec), formatTime(duration))
				: QStringLiteral("0:00 / 0:00");
			const QFontMetrics timeFm(timeFont);
			p.drawText(QRect(textLeft, barY - timeFm.height() - 3, textWidth, timeFm.height()),
				   Qt::AlignRight | Qt::AlignVCenter, timeStr);

			const QRectF trackRect(textLeft, barY, textWidth, barHeight);
			QPainterPath trackPath;
			trackPath.addRoundedRect(trackRect, barHeight / 2.0, barHeight / 2.0);
			p.fillPath(trackPath, QColor(255, 255, 255, 45));

			if (ratio > 0.0) {
				const qreal fillW = std::max(static_cast<qreal>(barHeight), textWidth * ratio);
				const QRectF fillRect(textLeft, barY, std::min(static_cast<qreal>(textWidth), fillW), barHeight);
				QPainterPath fillPath;
				fillPath.addRoundedRect(fillRect, barHeight / 2.0, barHeight / 2.0);
				p.fillPath(fillPath, obsColorToQColor(accentColor));
			}
		}

		p.end();
	}

	void uploadTexture()
	{
		if (canvas.isNull()) {
			return;
		}
		const uint32_t w = static_cast<uint32_t>(canvas.width());
		const uint32_t h = static_cast<uint32_t>(canvas.height());
		const uint8_t *bits = canvas.constBits();

		if (!texture || texWidth != w || texHeight != h) {
			if (texture) {
				gs_texture_destroy(texture);
				texture = nullptr;
			}
			texture = gs_texture_create(w, h, GS_RGBA, 1, &bits, GS_DYNAMIC);
			texWidth = w;
			texHeight = h;
		} else {
			gs_texture_set_image(texture, bits, w * 4, false);
		}
	}
};

const char *ytmd_get_name(void *)
{
	return obs_module_text("YtmDesktopSource");
}

void ytmd_update(void *data, obs_data_t *settings)
{
	auto *src = static_cast<YtmdSource *>(data);

	const QString newHost = QString::fromUtf8(obs_data_get_string(settings, kPropHost));
	const int newPort = static_cast<int>(obs_data_get_int(settings, kPropPort));
	const QString newToken = QString::fromUtf8(obs_data_get_string(settings, kPropToken));

	src->width = static_cast<uint32_t>(std::clamp<long long>(obs_data_get_int(settings, kPropWidth), 200, 1920));
	src->height = static_cast<uint32_t>(std::clamp<long long>(obs_data_get_int(settings, kPropHeight), 64, 600));
	src->showAlbumArt = obs_data_get_bool(settings, kPropShowArt);
	src->showProgressBar = obs_data_get_bool(settings, kPropShowProgress);
	src->hideWhenPaused = obs_data_get_bool(settings, kPropHidePaused);
	src->bgColor = static_cast<uint32_t>(obs_data_get_int(settings, kPropBgColor));
	src->bgOpacity = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, kPropBgOpacity), 0, 100));
	src->accentColor = static_cast<uint32_t>(obs_data_get_int(settings, kPropAccentColor));
	src->textColor = static_cast<uint32_t>(obs_data_get_int(settings, kPropTextColor));
	src->subTextColor = static_cast<uint32_t>(obs_data_get_int(settings, kPropSubTextColor));
	src->fontSize = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, kPropFontSize), 8, 72));
	src->cornerRadius = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, kPropCornerRadius), 0, 64));

	if (newHost != src->host || newPort != src->port || newToken != src->token) {
		src->host = newHost;
		src->port = newPort;
		src->token = newToken;
		if (src->client) {
			src->client->setConfig(src->host, src->port, src->token);
		}
	}

	src->settingsVersion++;
}

void *ytmd_create(obs_data_t *settings, obs_source_t *source)
{
	auto *src = new YtmdSource();
	src->source = source;
	src->client = std::make_unique<YtmdClient>();

	src->client->setTokenCallback([source](const QString &newToken) {
		obs_data_t *s = obs_source_get_settings(source);
		obs_data_set_string(s, kPropToken, newToken.toUtf8().constData());
		obs_source_update(source, s);
		obs_data_release(s);
		obs_source_update_properties(source);
	});

	ytmd_update(src, settings);
	src->client->setConfig(src->host, src->port, src->token);
	return src;
}

void ytmd_destroy(void *data)
{
	auto *src = static_cast<YtmdSource *>(data);
	if (src->client) {
		src->client->setTokenCallback(nullptr);
		src->client.reset();
	}
	if (src->texture) {
		obs_enter_graphics();
		gs_texture_destroy(src->texture);
		src->texture = nullptr;
		obs_leave_graphics();
	}
	delete src;
}

void ytmd_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, kPropHost, "127.0.0.1");
	obs_data_set_default_int(settings, kPropPort, 9863);
	obs_data_set_default_string(settings, kPropToken, "");
	obs_data_set_default_int(settings, kPropWidth, 520);
	obs_data_set_default_int(settings, kPropHeight, 136);
	obs_data_set_default_bool(settings, kPropShowArt, true);
	obs_data_set_default_bool(settings, kPropShowProgress, true);
	obs_data_set_default_bool(settings, kPropHidePaused, false);
	obs_data_set_default_int(settings, kPropBgColor, 0x181818);
	obs_data_set_default_int(settings, kPropBgOpacity, 88);
	obs_data_set_default_int(settings, kPropAccentColor, 0x3A3AFF);
	obs_data_set_default_int(settings, kPropTextColor, 0xFFFFFF);
	obs_data_set_default_int(settings, kPropSubTextColor, 0xBBBBBB);
	obs_data_set_default_int(settings, kPropFontSize, 18);
	obs_data_set_default_int(settings, kPropCornerRadius, 14);
}

bool on_pair_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	auto *src = static_cast<YtmdSource *>(data);
	if (src && src->client) {
		src->client->requestPairing();
	}
	return true;
}

obs_properties_t *ytmd_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(props, kPropHost, obs_module_text("Host"), OBS_TEXT_DEFAULT);
	obs_properties_add_int(props, kPropPort, obs_module_text("Port"), 1, 65535, 1);
	obs_properties_add_text(props, kPropToken, obs_module_text("Token"), OBS_TEXT_PASSWORD);
	obs_properties_add_button(props, kPropPairBtn, obs_module_text("PairButton"), on_pair_clicked);

	auto *src = static_cast<YtmdSource *>(data);
	QString status = QStringLiteral("Initializing...");
	if (src && src->client) {
		const YtmdSnapshot snap = src->client->snapshot();
		if (!snap.statusText.isEmpty()) {
			status = snap.statusText;
		}
	}
	obs_property_t *statusProp = obs_properties_add_text(
		props, kPropStatus, obs_module_text("PairingStatus"), OBS_TEXT_INFO);
	obs_property_text_set_info_type(statusProp, OBS_TEXT_INFO_NORMAL);
	if (src) {
		obs_data_t *s = obs_source_get_settings(src->source);
		obs_data_set_string(s, kPropStatus, status.toUtf8().constData());
		obs_data_release(s);
	}

	obs_properties_add_int(props, kPropWidth, obs_module_text("Width"), 200, 1920, 4);
	obs_properties_add_int(props, kPropHeight, obs_module_text("Height"), 64, 600, 4);
	obs_properties_add_bool(props, kPropShowArt, obs_module_text("ShowAlbumArt"));
	obs_properties_add_bool(props, kPropShowProgress, obs_module_text("ShowProgressBar"));
	obs_properties_add_bool(props, kPropHidePaused, obs_module_text("HideWhenPaused"));

	obs_properties_add_color(props, kPropBgColor, obs_module_text("BgColor"));
	obs_properties_add_int_slider(props, kPropBgOpacity, obs_module_text("BgOpacity"), 0, 100, 1);
	obs_properties_add_color(props, kPropAccentColor, obs_module_text("AccentColor"));
	obs_properties_add_color(props, kPropTextColor, obs_module_text("TextColor"));
	obs_properties_add_color(props, kPropSubTextColor, obs_module_text("SubTextColor"));
	obs_properties_add_int_slider(props, kPropFontSize, obs_module_text("FontSize"), 8, 48, 1);
	obs_properties_add_int_slider(props, kPropCornerRadius, obs_module_text("CornerRadius"), 0, 48, 1);

	return props;
}

uint32_t ytmd_get_width(void *data)
{
	auto *src = static_cast<YtmdSource *>(data);
	return src->width;
}

uint32_t ytmd_get_height(void *data)
{
	auto *src = static_cast<YtmdSource *>(data);
	return src->height;
}

void ytmd_video_tick(void *data, float)
{
	auto *src = static_cast<YtmdSource *>(data);
	if (!src->client) {
		return;
	}

	const YtmdSnapshot snap = src->client->snapshot();
	src->visible = !(src->hideWhenPaused && (!snap.track.valid || snap.track.trackState != 1));
	if (!src->visible) {
		return;
	}

	double elapsed = snap.track.videoProgress;
	if (snap.track.valid && snap.track.trackState == 1 && snap.lastPollMs > 0) {
		const qint64 dtMs = QDateTime::currentMSecsSinceEpoch() - snap.lastPollMs;
		if (dtMs > 0 && dtMs < 5000) {
			elapsed += static_cast<double>(dtMs) / 1000.0;
		}
	}
	if (snap.track.durationSeconds > 0.0) {
		elapsed = std::clamp(elapsed, 0.0, snap.track.durationSeconds);
	}

	const int currentSec = static_cast<int>(std::floor(elapsed));
	const int progressPx = (snap.track.durationSeconds > 0.0)
		? static_cast<int>((elapsed / snap.track.durationSeconds) * static_cast<double>(src->width))
		: 0;

	const bool dirty = (snap.version != src->lastRenderedVersion) ||
		(src->settingsVersion != src->lastRenderedSettings) ||
		(src->showProgressBar && snap.track.valid &&
		 (currentSec != src->lastRenderedSecond || progressPx != src->lastProgressPx));

	if (dirty) {
		src->renderCard(snap, elapsed);
		obs_enter_graphics();
		src->uploadTexture();
		obs_leave_graphics();

		src->lastRenderedVersion = snap.version;
		src->lastRenderedSettings = src->settingsVersion;
		src->lastRenderedSecond = currentSec;
		src->lastProgressPx = progressPx;
	}
}

void ytmd_video_render(void *data, gs_effect_t *)
{
	auto *src = static_cast<YtmdSource *>(data);
	if (!src->visible || !src->texture) {
		return;
	}

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_technique_t *tech = gs_effect_get_technique(effect, "Draw");

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), src->texture);
	gs_draw_sprite(src->texture, 0, src->texWidth, src->texHeight);

	gs_technique_end_pass(tech);
	gs_technique_end(tech);
}

} // namespace

void register_ytmd_source()
{
	obs_source_info info = {};
	info.id = "ytmdesktop_now_playing";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = ytmd_get_name;
	info.create = ytmd_create;
	info.destroy = ytmd_destroy;
	info.get_defaults = ytmd_get_defaults;
	info.get_properties = ytmd_get_properties;
	info.update = ytmd_update;
	info.get_width = ytmd_get_width;
	info.get_height = ytmd_get_height;
	info.video_tick = ytmd_video_tick;
	info.video_render = ytmd_video_render;

	obs_register_source(&info);
}
