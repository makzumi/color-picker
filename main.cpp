#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QScreen>
#include <QPixmap>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QClipboard>
#include <QCursor>
#include <QMessageBox>
#include <QTimer>
#include <QMap>
#include <QDebug>
#include <QProcess>
#include <QTemporaryDir>
#include <QWindow>
#include <QActionGroup>
#include <QFile>
#include <QSettings>

enum class ColorFormat {
    HTML,       // RRGGBB (hex without #)
    HEX,        // #RRGGBB
    DelphiHex,  // $00BBGGRR
    VBHex,      // &H00BBGGRR&
    RGBA,       // rgba(r, g, b, a)
    RGB,        // rgb(r, g, b)
    RGBFloat,   // r.rrr, g.ggg, b.bbb
    HSV,        // hsv(h, s%, v%)
    HSL         // hsl(h, s%, l%)
};

QString formatColor(const QColor& color, ColorFormat format) {
    switch (format) {
        case ColorFormat::HTML:
            return color.name().toUpper().mid(1);  // Remove #
        case ColorFormat::HEX:
            return color.name().toUpper();
        case ColorFormat::DelphiHex:
            return QString("$00%1%2%3")
                .arg(color.blue(), 2, 16, QChar('0'))
                .arg(color.green(), 2, 16, QChar('0'))
                .arg(color.red(), 2, 16, QChar('0'))
                .toUpper();
        case ColorFormat::VBHex:
            return QString("&H00%1%2%3&")
                .arg(color.blue(), 2, 16, QChar('0'))
                .arg(color.green(), 2, 16, QChar('0'))
                .arg(color.red(), 2, 16, QChar('0'))
                .toUpper();
        case ColorFormat::RGBA:
            return QString("rgba(%1, %2, %3, %4)")
                .arg(color.red())
                .arg(color.green())
                .arg(color.blue())
                .arg(color.alphaF(), 0, 'f', 2);
        case ColorFormat::RGB:
            return QString("rgb(%1, %2, %3)")
                .arg(color.red())
                .arg(color.green())
                .arg(color.blue());
        case ColorFormat::RGBFloat:
            return QString("%1, %2, %3")
                .arg(color.redF(), 0, 'f', 3)
                .arg(color.greenF(), 0, 'f', 3)
                .arg(color.blueF(), 0, 'f', 3);
        case ColorFormat::HSV:
            return QString("hsv(%1, %2%, %3%)")
                .arg(color.hsvHue())
                .arg(color.hsvSaturation() * 100 / 255)
                .arg(color.value() * 100 / 255);
        case ColorFormat::HSL:
            return QString("hsl(%1, %2%, %3%)")
                .arg(color.hslHue())
                .arg(color.hslSaturation() * 100 / 255)
                .arg(color.lightness() * 100 / 255);
    }
    return color.name().toUpper().mid(1);
}

class ColorPickerOverlay : public QWidget {
    Q_OBJECT

   public:
    ColorPickerOverlay(const QPixmap& screenshot, QScreen* screen, ColorFormat format)
        : screenshot_(screenshot), screen_(screen), colorFormat_(format), zoomFactor_(12), lastCursor_(-1000, -1000) {
        setWindowFlags(Qt::FramelessWindowHint |
                       Qt::WindowStaysOnTopHint |
                       Qt::Tool |
                       Qt::BypassWindowManagerHint);
        setAutoFillBackground(false);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_NoSystemBackground);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);

        // Set geometry to match this screen
        setGeometry(screen->geometry());

        // Single display pixmap that we update
        displayPixmap_ = screenshot_.copy();

        // Update timer for magnifier only
        updateTimer_ = new QTimer(this);
        connect(updateTimer_, &QTimer::timeout, this, [this]() {
            QPoint globalCursor = QCursor::pos();
            if (screen_->geometry().contains(globalCursor)) {
                QPoint localCursor = globalCursor - screen_->geometry().topLeft();
                // Only update if cursor actually moved
                if (localCursor != lastCursor_) {
                    lastCursor_ = localCursor;
                    updateDisplay();
                }
            } else {
                // Cursor left this screen - hide magnifier
                if (lastCursor_ != QPoint(-1000, -1000)) {
                    lastCursor_ = QPoint(-1000, -1000);
                    // Redraw with just the screenshot (no magnifier)
                    displayPixmap_ = screenshot_.copy();
                    update();
                }
            }
        });
        updateTimer_->start(16);  // ~60 FPS
    }

    ~ColorPickerOverlay() {
        // Explicitly clear pixmaps to free memory immediately
        screenshot_ = QPixmap();
        displayPixmap_ = QPixmap();
    }

   protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.drawPixmap(0, 0, displayPixmap_);
    }

    void updateDisplay() {
        // Start fresh from the screenshot
        displayPixmap_ = screenshot_.copy();

        QPainter painter(&displayPixmap_);

        // Get cursor position (already verified to be on this screen)
        QPoint cursor = lastCursor_;

        drawMagnifier(painter, cursor);

        // Draw crosshair
        painter.setPen(QPen(Qt::white, 2));
        painter.drawLine(cursor.x() - 10, cursor.y(), cursor.x() + 10, cursor.y());
        painter.drawLine(cursor.x(), cursor.y() - 10, cursor.x(), cursor.y() + 10);

        painter.end();

        // Trigger repaint
        update();
    }

    void drawMagnifier(QPainter& painter, const QPoint& cursor) {
        int magnifierSize = 150;
        int offset = 20;

        // Position magnifier near cursor (adjust to stay on screen)
        QPoint magnifierPos = cursor + QPoint(offset, offset);
        if (magnifierPos.x() + magnifierSize > width())
            magnifierPos.setX(cursor.x() - magnifierSize - offset);
        if (magnifierPos.y() + magnifierSize > height())
            magnifierPos.setY(cursor.y() - magnifierSize - offset);

        // Extract region around cursor from the original screenshot.
        // Important: use an odd number of pixels so there is a real center pixel.
        int capturePixels = magnifierSize / zoomFactor_;
        if (capturePixels < 3) capturePixels = 3;
        if ((capturePixels % 2) == 0) capturePixels -= 1;
        int radius = capturePixels / 2;

        QRect sourceRect(cursor.x() - radius,
                         cursor.y() - radius,
                         capturePixels, capturePixels);

        // Ensure source rect is within bounds
        sourceRect = sourceRect.intersected(screenshot_.rect());

        if (sourceRect.isEmpty()) return;

        // ### commented this out, border is not working as expected, might fix later
        // Draw magnifier background
        // painter.setPen(QPen(Qt::black, 3));
        // painter.setBrush(Qt::white);
        // painter.drawRect(magnifierPos.x(), magnifierPos.y(),
        //                  magnifierSize, magnifierSize);

        // Draw zoomed image from original screenshot with no smoothing
        QPixmap zoomed = screenshot_.copy(sourceRect)
                             .scaled(sourceRect.width() * zoomFactor_,
                                     sourceRect.height() * zoomFactor_,
                                     Qt::IgnoreAspectRatio,
                                     Qt::FastTransformation);

        // Center the zoomed image in the magnifier
        int xOffset = (magnifierSize - zoomed.width()) / 2;
        int yOffset = (magnifierSize - zoomed.height()) / 2;
        painter.drawPixmap(magnifierPos.x() + xOffset, magnifierPos.y() + yOffset, zoomed);

        // pixel indicatoor
        QPoint cursorInSource = cursor - sourceRect.topLeft();
        QRect pixelRect(magnifierPos.x() + xOffset + cursorInSource.x() * zoomFactor_,
                        magnifierPos.y() + yOffset + cursorInSource.y() * zoomFactor_,
                        zoomFactor_, zoomFactor_);

        painter.setPen(QPen(Qt::red, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(pixelRect);

        // Get and display color at cursor from original screenshot
        QColor color = screenshot_.toImage().pixelColor(cursor);

        // Draw color info box - show only the selected format
        QString colorText = formatColor(color, colorFormat_);

        QRect textRect(magnifierPos.x(), magnifierPos.y() + magnifierSize + 5,
                       magnifierSize, 50);
        painter.fillRect(textRect, QColor(0, 0, 0, 200));

        // Draw color preview square
        int squareSize = 30;
        int squarePadding = 10;
        QRect colorSquare(textRect.left() + squarePadding,
                          textRect.top() + (textRect.height() - squareSize) / 2,
                          squareSize, squareSize);

        // Fill with the actual color
        painter.fillRect(colorSquare, color);

        // Draw white border around the square
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(colorSquare);

        // Draw text next to the color square
        painter.setPen(Qt::white);
        QRect textOnlyRect(colorSquare.right() + squarePadding, textRect.top(),
                           textRect.width() - colorSquare.width() - squarePadding * 3, textRect.height());
        painter.drawText(textOnlyRect, Qt::AlignLeft | Qt::AlignVCenter, colorText);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            // Get color from screenshot at click position
            QPoint localPos = event->pos();
            if (localPos.x() >= 0 && localPos.x() < screenshot_.width() &&
                localPos.y() >= 0 && localPos.y() < screenshot_.height()) {
                QColor color = screenshot_.toImage().pixelColor(localPos);

                // Copy to clipboard in selected format
                QString colorText = formatColor(color, colorFormat_);
                QClipboard* clipboard = QApplication::clipboard();
                clipboard->setText(colorText);

                emit colorPicked(colorText);
            }
            emit closeAllOverlays();
        } else if (event->button() == Qt::RightButton) {
            // Cancel
            emit closeAllOverlays();
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            emit closeAllOverlays();
        }
    }

   signals:
    void colorPicked(const QString& colorText);
    void closeAllOverlays();

   private:
    QPixmap screenshot_;     // Original screenshot
    QPixmap displayPixmap_;  // What we actually display (updated each frame)
    QScreen* screen_;
    ColorFormat colorFormat_;
    int zoomFactor_;
    QTimer* updateTimer_;
    QPoint lastCursor_;
};

class ColorPickerApp : public QObject {
    Q_OBJECT

   public:
    ColorPickerApp() : activeOverlays_(), currentFormat_(ColorFormat::HTML) {
        // load format
        QSettings settings;
        int savedFormat = settings.value("colorFormat", static_cast<int>(ColorFormat::HTML)).toInt();
        currentFormat_ = static_cast<ColorFormat>(savedFormat);

        // tray icon
        trayIcon_ = new QSystemTrayIcon(this);

        // get icon
        QIcon icon(":/icon.svg");

        trayIcon_->setIcon(icon);
        trayIcon_->setToolTip("Color Picker");

        // menu
        QMenu* menu = new QMenu();
        QAction* pickAction = menu->addAction("Pick Color");
        pickAction->setFont(QFont(pickAction->font().family(), pickAction->font().pointSize(), QFont::Bold));
        menu->addSeparator();

        // formats submenu
        QMenu* formatMenu = menu->addMenu("Color Format");
        QActionGroup* formatGroup = new QActionGroup(this);
        formatGroup->setExclusive(true);

        // based on the formats from the inspistation for this app, https://instant-eyedropper.com/
        addFormatAction(formatMenu, formatGroup, "HTML (RRGGBB)", ColorFormat::HTML, currentFormat_ == ColorFormat::HTML);
        addFormatAction(formatMenu, formatGroup, "HEX (#RRGGBB)", ColorFormat::HEX, currentFormat_ == ColorFormat::HEX);
        addFormatAction(formatMenu, formatGroup, "Delphi Hex ($00BBGGRR)", ColorFormat::DelphiHex, currentFormat_ == ColorFormat::DelphiHex);
        addFormatAction(formatMenu, formatGroup, "Visual Basic Hex (&H00BBGGRR&)", ColorFormat::VBHex, currentFormat_ == ColorFormat::VBHex);
        addFormatAction(formatMenu, formatGroup, "RGBA", ColorFormat::RGBA, currentFormat_ == ColorFormat::RGBA);
        addFormatAction(formatMenu, formatGroup, "RGB", ColorFormat::RGB, currentFormat_ == ColorFormat::RGB);
        addFormatAction(formatMenu, formatGroup, "RGB Float", ColorFormat::RGBFloat, currentFormat_ == ColorFormat::RGBFloat);
        addFormatAction(formatMenu, formatGroup, "HSV (HSB)", ColorFormat::HSV, currentFormat_ == ColorFormat::HSV);
        addFormatAction(formatMenu, formatGroup, "HSL", ColorFormat::HSL, currentFormat_ == ColorFormat::HSL);

        menu->addSeparator();

        // autostart option
        // todo: move this to its own function
        QAction* autostartAction = menu->addAction("Start with Computer");
        autostartAction->setCheckable(true);
        autostartAction->setChecked(isAutostartEnabled());
        connect(autostartAction, &QAction::triggered, this, &ColorPickerApp::toggleAutostart);

        menu->addSeparator();
        QAction* quitAction = menu->addAction("Quit");

        connect(pickAction, &QAction::triggered, this, &ColorPickerApp::startColorPicker);
        connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
        connect(trayIcon_, &QSystemTrayIcon::activated,
                this, &ColorPickerApp::onTrayActivated);

        trayIcon_->setContextMenu(menu);
        trayIcon_->show();
    }

    void addFormatAction(QMenu* menu, QActionGroup* group, const QString& text, ColorFormat format, bool checked) {
        QAction* action = menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(checked);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, format]() {
            currentFormat_ = format;
            QSettings settings;
            settings.setValue("colorFormat", static_cast<int>(format));
        });
    }

   private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            startColorPicker();
        }
    }

    void startColorPicker() {
        // cleanup overlays
        closeAllOverlays();

        // screenshot using spectacle
        QList<QScreen*> screens = QGuiApplication::screens();
        if (screens.isEmpty()) return;

        QMap<QScreen*, QPixmap> screenshots;
        QTemporaryDir tempDir;

        if (!tempDir.isValid()) {
            return;
        }

        // get all screens
        QString fullScreenshot = tempDir.path() + "/fullscreen.png";
        QProcess spectacleProcess;
        // take screenshot of all screens
        spectacleProcess.start("spectacle", QStringList() << "-fb" << "-n" << "-o" << fullScreenshot);
        spectacleProcess.waitForFinished(2000);

        QPixmap fullCapture(fullScreenshot);

        if (!fullCapture.isNull()) {
            // split ss per screen
            for (QScreen* screen : screens) {
                QRect geo = screen->geometry();
                // desktop offsets
                QRect virtualGeo = screens.first()->geometry();
                for (QScreen* s : screens) {
                    virtualGeo = virtualGeo.united(s->geometry());
                }

                // position the parts
                QPoint offset = geo.topLeft() - virtualGeo.topLeft();
                QPixmap screenShot = fullCapture.copy(offset.x(), offset.y(),
                                                      geo.width(), geo.height());
                screenshots[screen] = screenShot;
            }
        } else {
            // no spectable found
            for (QScreen* screen : screens) {
                QRect geo = screen->geometry();
                QPixmap screenshot(geo.size());
                screenshot.fill(QColor(60, 60, 60));
                QPainter p(&screenshot);
                p.setPen(Qt::white);
                p.setFont(QFont("Arial", 24));
                p.drawText(screenshot.rect(), Qt::AlignCenter,
                           "Screenshot not available\nPlease install 'spectacle'");
                p.end();
                screenshots[screen] = screenshot;
            }
        }

        // close tray menu
        QTimer::singleShot(150, this, [this, screenshots, screens]() {
            for (QScreen* screen : screens) {
                ColorPickerOverlay* overlay = new ColorPickerOverlay(screenshots[screen], screen, currentFormat_);

                connect(overlay, &ColorPickerOverlay::colorPicked,
                        this, &ColorPickerApp::onColorPicked);
                connect(overlay, &ColorPickerOverlay::closeAllOverlays,
                        this, &ColorPickerApp::closeAllOverlays);

                activeOverlays_.append(overlay);
                overlay->showFullScreen();
                overlay->raise();
                overlay->activateWindow();
            }
        });
    }

    void closeAllOverlays() {
        for (ColorPickerOverlay* overlay : activeOverlays_) {
            overlay->close();
            overlay->deleteLater();
        }
        activeOverlays_.clear();
    }

    void onColorPicked(const QString& colorText) {
        trayIcon_->showMessage("Color Picked!",
                               QString("Copied to clipboard: %1").arg(colorText),
                               QSystemTrayIcon::Information, 2000);
    }

   private:
    QString getAutostartPath() const {
        QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
        if (configHome.isEmpty()) {
            configHome = QDir::homePath() + "/.config";
        }
        return configHome + "/autostart/color-picker.desktop";
    }

    bool isAutostartEnabled() const {
        return QFile::exists(getAutostartPath());
    }

    void toggleAutostart() {
        QString autostartPath = getAutostartPath();
        QAction* action = qobject_cast<QAction*>(sender());

        if (QFile::exists(autostartPath)) {
            // autostart false
            if (QFile::remove(autostartPath)) {
                if (action) action->setChecked(false);
            }
        } else {
            // insert auto start
            QDir().mkpath(QFileInfo(autostartPath).absolutePath());

            QString execPath = QCoreApplication::applicationFilePath();
            QString desktopEntry = QString(
                                       "[Desktop Entry]\n"
                                       "Type=Application\n"
                                       "Name=Color Picker\n"
                                       "Exec=%1\n"
                                       "Icon=color-picker\n"
                                       "Terminal=false\n"
                                       "Categories=Utility;\n"
                                       "X-GNOME-Autostart-enabled=true\n")
                                       .arg(execPath);

            QFile file(autostartPath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(desktopEntry.toUtf8());
                file.close();
                if (action) action->setChecked(true);
            }
        }
    }

    QSystemTrayIcon* trayIcon_;
    QList<ColorPickerOverlay*> activeOverlays_;
    ColorFormat currentFormat_;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, "Color Picker",
                              "System tray is not available!");
        return 1;
    }

    ColorPickerApp pickerApp;

    return app.exec();
}

#include "main.moc"