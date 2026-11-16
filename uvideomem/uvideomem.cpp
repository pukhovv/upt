#include <QApplication>
#include <QLabel>
#include <QMutex>
#include <QMutexLocker>
#include <QScreen>
#include <QShortcut>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#include "uvideomem.h"
#include "upt/float3.h"
#include <stddef.h>

namespace {
// Display-only bilateral filter; the linear radiance average remains untouched.
static inline void denoise_row(uint32_t const *pixels, unsigned int width,
			       unsigned int height, unsigned int y, uint32_t *output)
{
	for (unsigned int x = 0; x < width; ++x) {
		float3 center = float3_frompixel(pixels[(size_t)y * width + x]);
		float3 sum = {0};
		float total = 0;
		for (int dy = -1; dy <= 1; ++dy) {
			int iy = (int)y + dy;
			unsigned int sy = iy < 0 ? 0 : iy >= (int)height ? height - 1 : (unsigned int)iy;
			for (int dx = -1; dx <= 1; ++dx) {
				int ix = (int)x + dx;
				unsigned int sx = ix < 0 ? 0 : ix >= (int)width ? width - 1 : (unsigned int)ix;
				float3 c = float3_frompixel(pixels[(size_t)sy * width + sx]);
				float3 delta = float3_sub(c, center);
				float spatial = (dx == 0 ? 2 : 1) * (dy == 0 ? 2 : 1);
				float weight = spatial / (1 + 64 * float3_dot(delta, delta));
				sum = float3_add(sum, float3_scale(c, weight));
				total += weight;
			}
		}
		output[x] = float3_topixel(float3_scale(sum, 1 / total));
	}
}

struct Video {
	QImage image;
	QImage filtered;
	std::vector<uint32_t> filtered_row;
	QMutex mutex;
	std::atomic<bool> stopped{false};
	bool dirty = false;
	bool denoise = true;
	unsigned int row = 0;
	unsigned long long frame = 0;
};
Video *video;

void filter_row(unsigned int y)
{
	denoise_row(reinterpret_cast<uint32_t const *>(video->image.constBits()),
		video->image.width(), video->image.height(), y, video->filtered_row.data());
	QMutexLocker lock(&video->mutex);
	std::memcpy(video->filtered.scanLine(y), video->filtered_row.data(),
		video->image.width() * sizeof(uint32_t));
}
}

extern "C" bool uvideomem_present(uint32_t const *row, unsigned int y,
				 unsigned long long frame)
{
	if (video->stopped.load()) return false;
	{
		QMutexLocker lock(&video->mutex);
		std::memcpy(video->image.scanLine(y), row, video->image.width() * sizeof(*row));
		video->row = y;
		video->frame = frame;
	}
	if (y > 0) filter_row(y - 1);
	if (y + 1 == (unsigned int)video->image.height()) filter_row(y);
	{
		QMutexLocker lock(&video->mutex);
		video->dirty = true;
	}
	return true;
}

extern "C" int uvideomem_init(void (*render)(void *, unsigned int, unsigned int), void *context)
{
	char name[] = "upt";
	char *argv[] = {name, nullptr};
	int argc = 1;
	QApplication app(argc, argv);
	QScreen *screen = app.primaryScreen();
	QSize pixels = screen->size() * screen->devicePixelRatio();
	Video state;
	state.image = QImage(pixels, QImage::Format_RGBX8888);
	state.filtered = QImage(pixels, QImage::Format_RGBX8888);
	state.filtered_row.resize(pixels.width());
	if (state.image.isNull() || state.filtered.isNull()) {
		std::fprintf(stderr, "Cannot allocate GUI framebuffer.\n");
		return 1;
	}
	state.image.setDevicePixelRatio(screen->devicePixelRatio());
	state.filtered.setDevicePixelRatio(screen->devicePixelRatio());
	state.image.fill(Qt::black);
	state.filtered.fill(Qt::black);
	video = &state;
	QLabel window(nullptr, Qt::Window);
	window.setWindowTitle("upt — progressive rendering");
	window.setAlignment(Qt::AlignCenter);
	window.winId();
	window.windowHandle()->setScreen(screen);
	window.setGeometry(screen->geometry());
	window.showFullScreen();
	QLabel status(&window);
	status.setStyleSheet("background: rgba(0,0,0,180); color: white; padding: 6px;");
	status.move(12, 12);
	status.setText("Rendering first pass…  Esc to close");
	status.adjustSize();
	status.show();
	QShortcut escape(QKeySequence(Qt::Key_Escape), &window);
	QObject::connect(&escape, &QShortcut::activated, &window, &QWidget::close);
	QShortcut denoise(QKeySequence(Qt::Key_D), &window);
	QObject::connect(&denoise, &QShortcut::activated, [&] {
		QMutexLocker lock(&state.mutex);
		state.denoise = !state.denoise;
		state.dirty = true;
	});
	QObject::connect(&app, &QApplication::aboutToQuit, [&] { state.stopped.store(true); });
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&] {
		QImage snapshot;
		unsigned int row;
		unsigned long long frame;
		{
			QMutexLocker lock(&state.mutex);
			if (!state.dirty) return;
			snapshot = (state.denoise ? state.filtered : state.image).copy();
			row = state.row;
			frame = state.frame;
			state.dirty = false;
		}
		window.setPixmap(QPixmap::fromImage(snapshot));
		status.setText(QString("%1 × %2  |  Pass %3: %4%  |  %5 completed samples/pixel  |  Denoise %6 (D)  |  Esc to close")
			.arg(pixels.width()).arg(pixels.height()).arg(frame + 1)
			.arg(100 * (row + 1) / pixels.height())
			.arg(frame * 8 + (row + 1 == (unsigned int)pixels.height() ? 8 : 0))
			.arg(state.denoise ? "on" : "off"));
		status.adjustSize();
	});
	timer.start(1000 / 30);
	std::fprintf(stderr, "GUI: %s, %d x %d pixels, indefinite rendering; Esc to close.\n",
		     screen->name().toLocal8Bit().constData(), pixels.width(), pixels.height());
	auto worker = QThread::create([&] { render(context, pixels.width(), pixels.height()); });
	worker->start();
	int result = app.exec();
	state.stopped.store(true);
	worker->wait();
	delete worker;
	video = nullptr;
	return result;
}
