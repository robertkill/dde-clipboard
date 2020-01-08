/*
 * Copyright (C) 2018 ~ 2025 Deepin Technology Co., Ltd.
 *
 * Author:     fanpengcheng <fanpengcheng_cm@deepin.com>
 *
 * Maintainer: fanpengcheng <fanpengcheng_cm@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "itemwidget.h"
#include "constants.h"
#include "pixmaplabel.h"
#include "refreshtimer.h"

#include <QPainter>
#include <QTextOption>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QApplication>
#include <QMouseEvent>
#include <QDir>
#include <QBitmap>
#include <QImageReader>
#include <QIcon>
#include <QScopedPointer>
#include <QFile>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QMimeDatabase>
#include <QGraphicsOpacityEffect>

#include <DFontSizeManager>
#include <DGuiApplicationHelper>

#include "dgiofile.h"
#include "dgiofileinfo.h"

#include <cmath>

#define HoverAlpha 160
#define UnHoverAlpha 80

QList<QRectF> getCornerGeometryList(const QRectF &baseRect, const QSizeF &cornerSize)
{
    QList<QRectF> list;
    int offset = int(baseRect.width() / 8);
    const QSizeF &offset_size = cornerSize / 2;

    list << QRectF(QPointF(std::floor(baseRect.right() - offset - offset_size.width()),
                           std::floor(baseRect.bottom() - offset - offset_size.height())), cornerSize);
    list << QRectF(QPointF(std::floor(baseRect.left() + offset - offset_size.width()), std::floor(list.first().top())), cornerSize);
    list << QRectF(QPointF(std::floor(list.at(1).left()), std::floor(baseRect.top() + offset - offset_size.height())), cornerSize);
    list << QRectF(QPointF(std::floor(list.first().left()), std::floor(list.at(2).top())), cornerSize);

    return list;
}

QPixmap getIconPixmap(const QIcon &icon, const QSize &size, qreal pixelRatio, QIcon::Mode mode, QIcon::State state)
{
    // ###(zccrs): 开启Qt::AA_UseHighDpiPixmaps后，QIcon::pixmap会自动执行 pixmapSize *= qApp->devicePixelRatio()
    //             而且，在有些QIconEngine的实现中，会去调用另一个QIcon::pixmap，导致 pixmapSize 在这种嵌套调用中越来越大
    //             最终会获取到一个是期望大小几倍的图片，由于图片太大，会很快将 QPixmapCache 塞满，导致后面再调用QIcon::pixmap
    //             读取新的图片时无法缓存，非常影响图片绘制性能。此处在获取图片前禁用 Qt::AA_UseHighDpiPixmaps，自行处理图片大小问题
    bool useHighDpiPixmaps = qApp->testAttribute(Qt::AA_UseHighDpiPixmaps);
    qApp->setAttribute(Qt::AA_UseHighDpiPixmaps, false);

    QSize icon_size = icon.actualSize(size, mode, state);

    if (icon_size.width() > size.width() || icon_size.height() > size.height())
        icon_size.scale(size, Qt::KeepAspectRatio);

    QSize pixmapSize = icon_size * pixelRatio;
    QPixmap px = icon.pixmap(pixmapSize, mode, state);

    // restore the value
    qApp->setAttribute(Qt::AA_UseHighDpiPixmaps, useHighDpiPixmaps);

    if (px.width() > icon_size.width() * pixelRatio) {
        px.setDevicePixelRatio(px.width() * 1.0 / qreal(icon_size.width()));
    } else if (px.height() > icon_size.height() * pixelRatio) {
        px.setDevicePixelRatio(px.height() * 1.0 / qreal(icon_size.height()));
    } else {
        px.setDevicePixelRatio(pixelRatio);
    }

    return px;
}

static QPixmap GetFileIcon(QString path)
{
    QFileInfo info(path);
    QIcon icon;

    if (!QFileInfo::exists(path)) {
        return QPixmap();
    }
    QScopedPointer<DGioFile> file(DGioFile::createFromPath(info.absoluteFilePath()));
    QExplicitlySharedDataPointer<DGioFileInfo> fileinfo = file->createFileInfo();
    if (!fileinfo) {
        return QPixmap();
    }

    QStringList icons = fileinfo->themedIconNames();
    if (!icons.isEmpty()) {
        icon =  QIcon::fromTheme(icons.first()).pixmap(FileIconWidth, FileIconWidth);
    } else {
        QString iconStr(fileinfo->iconString());
        if (iconStr.startsWith('/')) {
            icon = QIcon(iconStr);
        } else {
            icon = QIcon::fromTheme(iconStr);
        }
    }

    //get additional icons
    QPixmap pix = icon.pixmap(FileIconWidth, FileIconWidth);
    QList<QRectF> cornerGeometryList = getCornerGeometryList(pix.rect(), pix.size() / 4);
    QList<QIcon> iconList;
    if (info.isSymLink()) {
        iconList << QIcon::fromTheme("emblem-symbolic-link");
    }

    if (!info.isWritable()) {
        iconList << QIcon::fromTheme("emblem-readonly");
    }

    if (!info.isReadable()) {
        iconList << QIcon::fromTheme("emblem-unreadable");
    }

    //if info is shared()  add icon "emblem-shared"(code from 'dde-file-manager')

    QPainter painter(&pix);
    painter.setRenderHints(painter.renderHints() | QPainter::SmoothPixmapTransform);
    for (int i = 0; i < iconList.size(); ++i) {
        painter.drawPixmap(cornerGeometryList.at(i).toRect(),
                           getIconPixmap(iconList.at(i), QSize(24, 24), painter.device()->devicePixelRatioF(), QIcon::Normal, QIcon::On));
    }

    return pix;
}

static QPixmap GetFileIcon(const FileIconData &data)
{
    QIcon icon;
    QStringList icons = data.cornerIconList;
    QPixmap pix = data.fileIcon.pixmap(QSize(FileIconWidth, FileIconWidth));
    if (icons.isEmpty()) {
        return pix;
    }

    QPainter painter(&pix);
    painter.setRenderHints(painter.renderHints() | QPainter::SmoothPixmapTransform);
    QList<QRectF> cornerGeometryList = getCornerGeometryList(pix.rect(), pix.size() / 4);
    for (int i = 0; i < icons.size(); ++i) {
        painter.drawPixmap(cornerGeometryList.at(i).toRect(),
                           getIconPixmap(QIcon::fromTheme(icons.at(i)), QSize(24, 24), painter.device()->devicePixelRatioF(), QIcon::Normal, QIcon::On));
    }

    return pix;
}

/*!
 * \~chinese \class ItemWidget
 * \~chinese \brief 负责剪贴块数据的展示。
 * \~chinese 传入ItemData类型的数据，在initData之后，将数据分别设置相应的控件进行展示
 */
ItemWidget::ItemWidget(QPointer<ItemData> data, QWidget *parent)
    : DWidget(parent)
    , m_data(data)
    , m_nameLabel(new DLabel(this))
    , m_timeLabel(new DLabel(this))
    , m_closeButton(new IconButton(this))
    , m_contentLabel(new PixmapLabel(this))
    , m_statusLabel(new DLabel(this))
    , m_refreshTimer(new QTimer(this))
{
    initUI();
    initData(m_data);
    initConnect();
}

void ItemWidget::setText(const QString &text, const QString &length)
{
    QFont font = m_contentLabel->font();
    font.setItalic(true);
    m_contentLabel->setFont(font);
    m_contentLabel->setText(text);

    m_statusLabel->setText(length);
}

void ItemWidget::setPixmap(const QPixmap &pixmap)
{
    // 重新设置缩略图，用于更新随主题变化的缩略图边框颜色
    if (!m_pixmap.isNull() && (m_data->type() == ItemData::Image)) {
        QPixmap pix = Globals::pixmapScaled(pixmap);//先缩放,再设置圆角
        m_contentLabel->setPixmap(Globals::GetRoundPixmap(pix, palette().color(QPalette::Base)));
        m_statusLabel->setText(QString("%1X%2px").arg(pixmap.width()).arg(pixmap.height()));
    } else if (m_data->type() == ItemData::File) {
        QUrl url = m_data->urls().first();
        if (m_data->urls().size() == 1) {
            if (m_data->IconDataList().size() == m_data->urls().size()) {//先查看文件管理器在复制时有没有提供缩略图
                //图片不需要加角标,但需要对图片进行圆角处理(此时文件可能已经被删除，缩略图由文件管理器提供)
                if (QImageReader::supportedImageFormats().contains(QFileInfo(url.path()).suffix().toLatin1())) {
                    //只有图片需要圆角边框
                    FileIconData iconData = m_data->IconDataList().first();
                    iconData.cornerIconList.clear();
                    setFilePixmap(iconData, true);
                }
            } else {
                QMimeDatabase db;
                QMimeType mime = db.mimeTypeForFile(url.path());

                if (mime.name().startsWith("image/")) {//如果文件是图片,提供缩略图
                    QFile file(url.path());
                    QPixmap pix;
                    if (file.open(QFile::ReadOnly)) {
                        QByteArray array = file.readAll();
                        pix.loadFromData(array);

                        if (pix.isNull()) {
                            QIcon icon = QIcon::fromTheme(mime.genericIconName());
                            pix = icon.pixmap(PixmapWidth, PixmapHeight);
                            setFilePixmap(pix);
                        } else {
                            setFilePixmap(pix, true);
                        }
//                        pix = Globals::GetRoundPixmap(pix, palette().color(QPalette::Base));
                    } else {
                        QIcon icon = QIcon::fromTheme(mime.genericIconName());
                        pix = icon.pixmap(PixmapWidth, PixmapHeight);
                        setFilePixmap(pix);
                    }
                    file.close();
                } else {
                    setFilePixmap(GetFileIcon(url.path()));
                }
            }
        }
    }
}

void ItemWidget::setFilePixmap(const QPixmap &pixmap, bool setRadius)
{
    if (pixmap.size().isNull())
        return;
    QPixmap pix = pixmap;
    pix = Globals::pixmapScaled(pix);//如果需要加边框,先缩放再加边框
    if (setRadius) {
        pix = Globals::GetRoundPixmap(pix, palette().color(QPalette::Base));
    }
    m_contentLabel->setPixmap(pix);
}

void ItemWidget::setFilePixmap(const FileIconData &data, bool setRadius)
{
    QPixmap pix = GetFileIcon(data);
//    if (setRadius){
//        pix = Globals::pixmapScaled(pix);
//        pix = Globals::GetRoundPixmap(pix, palette().color(QPalette::Base));
//    }
    setFilePixmap(pix, setRadius);
}

void ItemWidget::setFilePixmaps(const QList<QPixmap> &list)
{
    m_contentLabel->setPixmapList(list);
}

void ItemWidget::setClipType(const QString &text)
{
    m_nameLabel->setText(text);
}

void ItemWidget::setCreateTime(const QDateTime &time)
{
    m_createTime = time;
    onRefreshTime();
}

void ItemWidget::setAlpha(int alpha)
{
    m_hoverAlpha = alpha;
    m_unHoverAlpha = alpha;

    update();
}

int ItemWidget::hoverAlpha() const
{
    return m_hoverAlpha;
}

void ItemWidget::setHoverAlpha(int alpha)
{
    m_hoverAlpha = alpha;

    update();
}

void ItemWidget::setOpacity(double opacity)
{
    QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect;
    setGraphicsEffect(effect);
    effect->setOpacity(opacity);
    effect->destroyed();
}

int ItemWidget::unHoverAlpha() const
{
    return m_unHoverAlpha;
}

void ItemWidget::setUnHoverAlpha(int alpha)
{
    m_unHoverAlpha = alpha;

    update();
}

void ItemWidget::setRadius(int radius)
{
    m_radius = radius;

    update();
}

void ItemWidget::onHoverStateChanged(bool hover)
{
    m_havor = hover;

    if (hover) {
        m_timeLabel->hide();
        m_closeButton->show();
    } else {
        m_timeLabel->show();
        m_closeButton->hide();
    }

    update();
}

void ItemWidget::onRefreshTime()
{
    m_timeLabel->setText(CreateTimeString(m_createTime));
}

void ItemWidget::onClose()
{
    QParallelAnimationGroup *group = new QParallelAnimationGroup(this);

    QPropertyAnimation *geoAni = new QPropertyAnimation(this, "geometry", group);
    geoAni->setStartValue(geometry());
    geoAni->setEndValue(QRect(geometry().center(), geometry().center()));
    geoAni->setDuration(AnimationTime);

    QPropertyAnimation *opacityAni = new QPropertyAnimation(this, "opacity", group);
    opacityAni->setStartValue(1.0);
    opacityAni->setEndValue(0.0);
    opacityAni->setDuration(AnimationTime);

    group->addAnimation(geoAni);
    group->addAnimation(opacityAni);

    m_data->remove();

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void ItemWidget::initUI()
{
    //标题区域
    QWidget *titleWidget = new QWidget;
    QHBoxLayout *titleLayout = new QHBoxLayout(titleWidget);
    titleLayout->setSpacing(0);
    titleLayout->setContentsMargins(10, 0, 10, 0);
    titleLayout->addWidget(m_nameLabel);
    titleLayout->addWidget(m_timeLabel);
    titleLayout->addWidget(m_closeButton);

    titleWidget->setFixedHeight(ItemTitleHeight);

    QFont font = DFontSizeManager::instance()->t4();
    font.setWeight(75);
    m_nameLabel->setFont(font);
    m_nameLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_timeLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    m_closeButton->setFixedSize(QSize(ItemTitleHeight, ItemTitleHeight) * 2 / 3);
    m_closeButton->setRadius(ItemTitleHeight);
    m_closeButton->setVisible(false);

    m_refreshTimer->setInterval(60 * 1000);

    //布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setMargin(0);
    mainLayout->addWidget(titleWidget, 0, Qt::AlignTop);

    QHBoxLayout *layout = new QHBoxLayout;
    layout->setContentsMargins(ContentMargin, 0, ContentMargin, 0);
    layout->addWidget(m_contentLabel, 0, Qt::AlignCenter);
    mainLayout->addLayout(layout, 0);
    mainLayout->addWidget(m_statusLabel, 0, Qt::AlignBottom);

    m_contentLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setFixedHeight(ItemStatusBarHeight);
    m_statusLabel->setAlignment(Qt::AlignCenter);

    setHoverAlpha(HoverAlpha);
    setUnHoverAlpha(UnHoverAlpha);
    setRadius(8);

    setFocusPolicy(Qt::StrongFocus);

    setMouseTracking(true);
    m_contentLabel->setMouseTracking(true);
    m_nameLabel->setMouseTracking(true);
    m_timeLabel->setMouseTracking(true);
    m_statusLabel->setMouseTracking(true);
}

void ItemWidget::initData(QPointer<ItemData> data)
{
    setClipType(data->title());
    setCreateTime(data->time());
    switch (data->type()) {
    case ItemData::Text: {
        setText(data->text(), data->subTitle());
    }
    break;
    case ItemData::Image: {
        m_contentLabel->setAlignment(Qt::AlignCenter);
        m_pixmap = data->pixmap();
        setPixmap(data->pixmap());
    }
    break;
    case ItemData::File: {
        if (data->urls().size() == 0)
            return;

        QUrl url = data->urls().first();
        if (data->urls().size() == 1) {
            if (data->IconDataList().size() == data->urls().size()) {//先查看文件管理器在复制时有没有提供缩略图
                //图片不需要加角标,但需要对图片进行圆角处理(此时文件可能已经被删除，缩略图由文件管理器提供)
                if (QImageReader::supportedImageFormats().contains(QFileInfo(url.path()).suffix().toLatin1())) {
                    //只有图片需要圆角边框
                    FileIconData iconData = data->IconDataList().first();
                    iconData.cornerIconList.clear();
                    setFilePixmap(iconData, true);
                } else {
                    setFilePixmap(data->IconDataList().first());
                }
            } else {
                QMimeDatabase db;
                QMimeType mime = db.mimeTypeForFile(url.path());

                if (mime.name().startsWith("image/")) { //如果文件是图片,提供缩略图
                    QFile file(url.path());
                    QPixmap pix;
                    if (file.open(QFile::ReadOnly)) {
                        QByteArray array = file.readAll();
                        pix.loadFromData(array);

                        if (pix.isNull()) {
                            QIcon icon = QIcon::fromTheme(mime.genericIconName());
                            pix = icon.pixmap(PixmapWidth, PixmapHeight);
                            setFilePixmap(pix);
                        } else {
                            setFilePixmap(pix, true);
                        }
//                        pix = Globals::GetRoundPixmap(pix, palette().color(QPalette::Base));
                    } else {
                        QIcon icon = QIcon::fromTheme(mime.genericIconName());
                        pix = icon.pixmap(PixmapWidth, PixmapHeight);
                        setFilePixmap(pix);
                    }
                    file.close();
                } else {
                    setFilePixmap(GetFileIcon(url.path()));
                }
            }

            QFontMetrics metrix = m_statusLabel->fontMetrics();
            QString text = metrix.elidedText(url.fileName(), Qt::ElideMiddle, WindowWidth - 2 * ItemMargin - 10, 0);
            m_statusLabel->setText(text);

        } else if (data->urls().size() > 1) {
            QFontMetrics metrix = m_statusLabel->fontMetrics();
            QString text = metrix.elidedText(tr("%1 files (%2...)").arg(data->urls().size()).arg(url.fileName()),
                                             Qt::ElideMiddle, WindowWidth - 2 * ItemMargin - 10, 0);
            m_statusLabel->setText(text);

            //判断文件管理器是否提供
            if (data->IconDataList().size() == data->urls().size()) {
                QList<QPixmap> pixmapList;
                foreach (auto iconData, data->IconDataList()) {
                    QPixmap pix = GetFileIcon(iconData);
                    pixmapList.push_back(pix);
                    if (pixmapList.size() == 3)
                        break;
                }
                qSort(pixmapList.begin(), pixmapList.end(), [ = ](const QPixmap & pix1, const QPixmap & pix2) {
                    return pix1.size().width() < pix2.size().width();
                });
                setFilePixmaps(pixmapList);
            } else {
                int iconNum = MIN(3, data->urls().size());
                QList<QPixmap> pixmapList;
                for (int i = 0; i < iconNum; ++i) {
                    QString filePath = data->urls()[i].toString();
                    if (filePath.startsWith("file://")) {
                        filePath = filePath.mid(QString("file://").length());
                    }
                    pixmapList.push_back(GetFileIcon(filePath));
                }
                setFilePixmaps(pixmapList);
            }
        }
    }
    break;
    default:
        break;
    }

    if (!data->dataEnabled()) {
        m_contentLabel->setEnabled(false);
        QFontMetrics metrix = m_statusLabel->fontMetrics();
        QString tips = tr("(File deleted)");
        int tipsWidth = metrix.width(tips);
        QString text = metrix.elidedText(m_statusLabel->text(), Qt::ElideMiddle, WindowWidth - 2 * ItemMargin - 10 - tipsWidth, 0);
        m_statusLabel->setText(text + tips);
    }
}

void ItemWidget::initConnect()
{
    connect(m_refreshTimer, &QTimer::timeout, this, &ItemWidget::onRefreshTime);
    m_refreshTimer->start();
    connect(RefreshTimer::instance(), &RefreshTimer::forceRefresh, this, &ItemWidget::onRefreshTime);
    connect(this, &ItemWidget::hoverStateChanged, this, &ItemWidget::onHoverStateChanged);
    connect(m_closeButton, &IconButton::clicked, this, &ItemWidget::onClose);
    connect(this, &ItemWidget::closeHasFocus, this, [&](bool has) {
        m_closeButton->setFocusState(has);
    });

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, [ = ] {
        setPixmap(m_pixmap);
    });
}

QString ItemWidget::CreateTimeString(const QDateTime &time)
{
    QString text;

    QDateTime t = QDateTime::currentDateTime();

    if (time.daysTo(t) >= 1 && time.daysTo(t) < 2) { //昨天发生的
        text = tr("Yesterday") + time.toString(" hh:mm");
    } else if (time.daysTo(t) >= 2 && time.daysTo(t) < 7) { //昨天以前，一周以内
        text = time.toString("ddd hh:mm");
    } else if (time.daysTo(t) >= 7) { //一周前以前的
        text = time.toString("yyyy/MM/dd");
    } else if (time.secsTo(t) < 60 && time.secsTo(t) >= 0) { //60秒以内
        text = tr("Just now");
    } else if (time.secsTo(t) >= 60 && time.secsTo(t) < 2 * 60) { //一分钟
        text = tr("1 minute ago");
    } else if (time.secsTo(t) >= 2 * 60 && time.secsTo(t) < 60 * 60) { //多少分钟前
        text = tr("%1 minutes ago").arg(time.secsTo(t) / 60);
    } else if (time.secsTo(t) >= 60 * 60 && time.secsTo(t) < 2 * 60 * 60) {//一小时前
        text = tr("1 hour ago");
    } else if (time.secsTo(t) >= 2 * 60 * 60 && time.daysTo(t) < 1) { //多少小时前(0点以后)
        text = tr("%1 hours ago").arg(time.secsTo(t) / 60 / 60);
    }

    return text;
}

void ItemWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_0:
        //表示切换‘焦点’，tab按键事件在delegate中已被拦截
        if (event->text() == "change focus") {
            Q_EMIT closeHasFocus(m_closeFocus = !m_closeFocus);
            return ;
        }
        break;
    case Qt::Key_Enter:
    case Qt::Key_Return: {
        if (m_closeFocus) {
            onClose();
        }
    }
    break;
    }

    return DWidget::keyPressEvent(event);
}

void ItemWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPalette pe = this->palette();
    QColor c = pe.color(QPalette::Base);

    QPen borderPen;
    borderPen.setColor(Qt::transparent);
    painter.setPen(borderPen);

    //裁剪绘制区域
    QPainterPath path;
    path.addRoundedRect(rect(), m_radius, m_radius);
    painter.setClipPath(path);

    //绘制标题区域
    QColor brushColor(c);
    brushColor.setAlpha(80);
    painter.setBrush(brushColor);
    painter.drawRect(QRect(0, 0, width(), ItemTitleHeight));

    //绘制背景
    brushColor.setAlpha(m_havor ? m_hoverAlpha : m_unHoverAlpha);
    painter.setBrush(brushColor);
    painter.drawRect(rect());

    return DWidget::paintEvent(event);
}

void ItemWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_data->dataEnabled()) {
        return DWidget::mousePressEvent(event);
    }

    if (m_data->type() == ItemData::File) {
        QList<QUrl> urls = m_data->urls();
        bool has = false;
        foreach (auto url, urls) {
            //mid(6)是为了去掉url里面的"file://"部分
            if (QDir().exists(url.toString().mid(6))) {
                has = true;
            }
        }
        if (!has) {
            m_data->setDataEnabled(false);
            //源文件被删除需要提示
            m_contentLabel->setEnabled(false);
            QFontMetrics metrix = m_statusLabel->fontMetrics();
            QString tips = tr("(File deleted)");
            int tipsWidth = metrix.width(tips);
            QString text = metrix.elidedText(m_statusLabel->text(), Qt::ElideMiddle, WindowWidth - 2 * ItemMargin - 10 - tipsWidth, 0);
            m_statusLabel->setText(text + tips);

            return DWidget::mousePressEvent(event);
        }
    }

    m_data->popTop();

    return DWidget::mousePressEvent(event);
}

void ItemWidget::enterEvent(QEvent *event)
{
    if (isEnabled()) {
        Q_EMIT hoverStateChanged(true);
    }

    return DWidget::enterEvent(event);
}

void ItemWidget::leaveEvent(QEvent *event)
{
    if (isEnabled()) {
        Q_EMIT closeHasFocus(false);
        Q_EMIT hoverStateChanged(false);
    }

    return DWidget::leaveEvent(event);
}

void ItemWidget::focusInEvent(QFocusEvent *event)
{
    if (isEnabled()) {
        Q_EMIT hoverStateChanged(true);
    }

    return DWidget::focusInEvent(event);
}

void ItemWidget::focusOutEvent(QFocusEvent *event)
{
    if (isEnabled()) {
        m_closeFocus = false;

        Q_EMIT hoverStateChanged(false);
        Q_EMIT closeHasFocus(false);
    }

    return DWidget::focusOutEvent(event);
}
