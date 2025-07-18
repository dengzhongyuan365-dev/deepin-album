/*
 * Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
 *
 * Author:     ZhangYong <zhangyong@uniontech.com>
 *
 * Maintainer: ZhangYong <ZhangYong@uniontech.com>
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
#include "imagedataservice.h"
#include "unionimage/unionimage.h"
#include "unionimage/baseutils.h"
#include "unionimage/unionimage_global.h"
#include "dbmanager/dbmanager.h"
#include "configsetter.h"
#include "movieservice.h"
#include <QDebug>

#include <QMetaType>
#include <QDirIterator>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

const QString SETTINGS_GROUP = "Thumbnail";
const QString SETTINGS_DISPLAY_MODE = "ThumbnailMode";
const int THUMBNAIL_MAX_SIZE = 180;

ImageDataService *ImageDataService::s_ImageDataService = nullptr;

ImageDataService *ImageDataService::instance(QObject *parent)
{
    Q_UNUSED(parent);
    if (!s_ImageDataService) {
        s_ImageDataService = new ImageDataService();
        qDebug() << "Created new ImageDataService instance";
    }
    return s_ImageDataService;
}

bool ImageDataService::pathInMap(const QString &path)
{
    QString loadModePath = getLoadModePath(path);
    auto iter = std::find_if(m_AllImageMap.begin(), m_AllImageMap.end(), [loadModePath](const std::pair<QString, QImage> &pr) {
        return pr.first == loadModePath;
    });
    bool found = iter != m_AllImageMap.end();
    return found;
}

std::pair<QImage, bool> ImageDataService::getImageFromMap(const QString &path)
{
    QMutexLocker locker(&m_imgDataMutex);

    QString loadModePath = getLoadModePath(path);

    auto iter = std::find_if(m_AllImageMap.begin(), m_AllImageMap.end(), [loadModePath](const std::pair<QString, QImage> &pr) {
        return pr.first == loadModePath;
    });
    if (iter != m_AllImageMap.end()) {
        return std::make_pair(iter->second, true);
    } else {
        qDebug() << "Image not found in map for path:" << path;
        return std::make_pair(QImage(), false);
    }
}

void ImageDataService::removePathFromMap(const QString &path)
{
    QMutexLocker locker(&m_imgDataMutex);

    auto iter = std::find_if(m_AllImageMap.begin(), m_AllImageMap.end(), [path](const std::pair<QString, QImage> &pr) {
        return pr.first == path;
    });
    if (iter != m_AllImageMap.end()) {
        qDebug() << "Removing path from map:" << path;
        m_AllImageMap.erase(iter);
    }

    QString scalPath = getScaledPath(path);
    iter = std::find_if(m_AllImageMap.begin(), m_AllImageMap.end(), [scalPath](const std::pair<QString, QImage> &pr) {
        return pr.first == scalPath;
    });
    if (iter != m_AllImageMap.end()) {
        qDebug() << "Removing scaled path from map:" << scalPath;
        m_AllImageMap.erase(iter);
    }
}

void ImageDataService::removeThumbnailFile(const QString &path)
{
    if (path.isEmpty()) {
        qWarning() << "Attempted to remove thumbnail for empty path";
        return;
    }

    QString thumbnailPath = Libutils::base::filePathToThumbnailPath(path);
    QString thumbnailScalePath = ImageDataService::instance()->getLoadModePath(thumbnailPath);
    if (QFile::exists(thumbnailPath)) {
        QFile::remove(thumbnailPath);
        qDebug() << "Removed thumbnail file:" << thumbnailPath;
    }
    if (QFile::exists(thumbnailScalePath)) {
        QFile::remove(thumbnailScalePath);
        qDebug() << "Removed scaled thumbnail file:" << thumbnailScalePath;
    }
}

QString ImageDataService::getLoadModePath(const QString &path)
{
    if (m_loadMode == 0)
        return path;

    return getScaledPath(path);
}

QString ImageDataService::getScaledPath(const QString &path)
{
    QFileInfo fi(path);
    QString tmpPath = fi.absolutePath() + "/" + fi.fileName().left(fi.fileName().lastIndexOf('.')) + "_scale" + fi.fileName().mid(fi.fileName().lastIndexOf('.'));
    return tmpPath;
}

void ImageDataService::addImage(const QString &path, const QImage &image)
{
    QMutexLocker locker(&m_imgDataMutex);

    QString loadModePath = getLoadModePath(path);

    auto iter = std::find_if(m_AllImageMap.begin(), m_AllImageMap.end(), [loadModePath](const std::pair<QString, QImage> &pr) {
        if (pr.first != loadModePath) {
            return false;
        }
        return true;
    });

    if (iter != m_AllImageMap.end()) {
        qDebug() << "Updating existing image in map for path:" << path;
        iter->second = image;
    } else {
        qDebug() << "Adding new image to map for path:" << path;
        m_AllImageMap.push_back(std::make_pair(loadModePath, image));
        if (m_AllImageMap.size() > 500) {
            qDebug() << "Image map size exceeded 500, removing oldest entry";
            m_AllImageMap.pop_front();
        }
    }
}

void ImageDataService::addMovieDurationStr(const QString &path, const QString &durationStr)
{
    QMutexLocker locker(&m_imgDataMutex);
    m_movieDurationStrMap[path] = durationStr;
    qDebug() << "Added movie duration for path:" << path << "duration:" << durationStr;
}

QString ImageDataService::getMovieDurationStrByPath(const QString &path)
{
    QMutexLocker locker(&m_imgDataMutex);
    bool hasDuration = m_movieDurationStrMap.contains(path);
    return hasDuration ? m_movieDurationStrMap[path] : QString();
}

bool ImageDataService::imageIsLoaded(const QString &path, bool isTrashFile)
{
    QMutexLocker locker(&m_imgDataMutex);

    bool loaded = false;
    if (isTrashFile) {
        QString realPath = Libutils::base::getDeleteFullPath(Libutils::base::hashByString(path), DBImgInfo::getFileNameFromFilePath(path));
        loaded = pathInMap(realPath) || pathInMap(path);
    } else {
        loaded = pathInMap(path);
    }
    return loaded;
}

ImageDataService::ImageDataService(QObject *parent) : QObject(parent)
{
    qDebug() << "Initializing ImageDataService";
    m_loadMode = 1;
    readThumbnailManager = new ReadThumbnailManager;
    readThread = new QThread;
    readThumbnailManager->moveToThread(readThread);
    readThread->start();
    connect(this, &ImageDataService::startImageLoad, readThumbnailManager, &ReadThumbnailManager::readThumbnail);

    //初始化的时候读取上次退出时的状态
    m_loadMode = LibConfigSetter::instance()->value(SETTINGS_GROUP, SETTINGS_DISPLAY_MODE, 0).toInt();
    qDebug() << "Initial load mode set to:" << m_loadMode;
}

void ImageDataService::stopFlushThumbnail()
{
    qDebug() << "Stopping thumbnail flush";
    readThumbnailManager->stopRead();
}

void ImageDataService::waitFlushThumbnailFinish()
{
    qDebug() << "Waiting for thumbnail flush to finish";
    while (ImageDataService::instance()->readThumbnailManager->isRunning());
    qDebug() << "Thumbnail flush finished";
}

bool ImageDataService::readerIsRunning()
{
    bool running = readThumbnailManager->isRunning();
    qDebug() << "Thumbnail reader running status:" << running;
    return running;
}

void ImageDataService::switchLoadMode()
{
    int oldMode = m_loadMode;
    switch (m_loadMode) {
    case 0:
        m_loadMode = 1;
        break;
    case 1:
        m_loadMode = 0;
        break;
    default:
        m_loadMode = 0;
        break;
    }

    //切完以后保存状态
    LibConfigSetter::instance()->setValue(SETTINGS_GROUP, SETTINGS_DISPLAY_MODE, m_loadMode.load());
    qDebug() << "Switched load mode from" << oldMode << "to" << m_loadMode;
}

int ImageDataService::getLoadMode()
{
    return m_loadMode;
}

QImage ImageDataService::getThumnailImageByPathRealTime(const QString &path, bool isTrashFile, bool bReload/* = false*/)
{
    QString realPath;

    if (!isTrashFile) {
        realPath = path;
        if (!QFile::exists(realPath)) {
            qWarning() << "File does not exist:" << realPath;
            return QImage();
        }
    } else {
        realPath = Libutils::base::getDeleteFullPath(Libutils::base::hashByString(path), DBImgInfo::getFileNameFromFilePath(path));
        if (!QFile::exists(realPath)) {
            if (!QFile::exists(path)) {
                qWarning() << "Trash file does not exist:" << path;
                return QImage();
            } else {
                realPath = path;
            }
        }
    }

    // 重新加载缩略图，清楚缓存对应缩略图
    if (bReload) {
        qDebug() << "Reloading thumbnail for path:" << realPath;
        removePathFromMap(realPath);
        removeThumbnailFile(realPath);
    }

    //尝试在缓存里面找图
    auto bufferImage = getImageFromMap(realPath);
    if (bufferImage.second) {
        return bufferImage.first;
    }

    //缓存没找到则加入图片到加载队列
    qDebug() << "Adding path to thumbnail load queue:" << realPath;
    readThumbnailManager->addLoadPath(realPath);

    //如果加载队列正在休眠，则发信号唤醒，反之不去反复发信号激活队列
    if (!readThumbnailManager->isRunning()) {
        qDebug() << "Waking up thumbnail reader thread";
        emit startImageLoad();
    }

    return QImage();
}

ReadThumbnailManager::ReadThumbnailManager(QObject *parent)
    : QObject(parent)
    , runningFlag(false)
    , stopFlag(false)
{
    qDebug() << "ReadThumbnailManager initialized";
}

void ReadThumbnailManager::addLoadPath(const QString &path)
{
    mutex.lock();
    needLoadPath.push_back(path);
    if (needLoadPath.size() > 100) {
        qDebug() << "Load path queue exceeded 100 items, removing oldest";
        needLoadPath.pop_front();
    }
    mutex.unlock();
}

void ReadThumbnailManager::readThumbnail()
{
    qDebug() << "Starting thumbnail read process";
    int sendCounter = 0; //刷新上层界面指示
    runningFlag = true;  //告诉外面加载队列处于激活状态

    while (1) {
        //尝试读取队列数据
        mutex.lock();

        if (needLoadPath.empty() || stopFlag) {
            mutex.unlock();
            break;
        }

        //锁定文件操作权限
        DBManager::m_fileMutex.lockForRead();

        auto path = needLoadPath[needLoadPath.size() - 1];
        needLoadPath.pop_back();

        mutex.unlock();

        sendCounter++;
        if (sendCounter == 5) { //每加载5张图，就让上层界面主动刷新一次
            sendCounter = 0;
            emit ImageDataService::instance()->sigeUpdateListview();
        }

        if (!QFileInfo(path).exists()) {
            qWarning() << "File no longer exists:" << path;
            DBManager::m_fileMutex.unlock();
            continue;
        }

        using namespace LibUnionImage_NameSpace;
        QImage tImg;
        QString srcPath = path;
        QString thumbnailPath = Libutils::base::filePathToThumbnailPath(path);
        thumbnailPath = ImageDataService::instance()->getLoadModePath(thumbnailPath);

        QFileInfo thumbnailFile(thumbnailPath);
        QString errMsg;
        if (thumbnailFile.exists()) {
            qDebug() << "Loading existing thumbnail:" << thumbnailPath;
            if (!loadStaticImageFromFile(thumbnailPath, tImg, errMsg, "PNG")) {
                qWarning() << "Failed to load thumbnail:" << errMsg;
                //不正常退出导致的缩略图损坏，删除原文件后重新尝试制作
                QFile::remove(thumbnailPath);
                if (!loadStaticImageFromFile(srcPath, tImg, errMsg)) {
                    qWarning() << "Failed to load source image:" << errMsg;
                }
            }

            if (isVideo(srcPath)) {
                qDebug() << "Getting video info for:" << srcPath;
                MovieInfo mi = MovieService::instance()->getMovieInfo(QUrl::fromLocalFile(srcPath));
                ImageDataService::instance()->addMovieDurationStr(srcPath, mi.duration);
            }
        } else {
            qDebug() << "Generating new thumbnail for:" << srcPath;
            //读图
            if (isVideo(srcPath)) {
                tImg = MovieService::instance()->getMovieCover(QUrl::fromLocalFile(srcPath));

                //获取视频信息 demo
                MovieInfo mi = MovieService::instance()->getMovieInfo(QUrl::fromLocalFile(srcPath));
                ImageDataService::instance()->addMovieDurationStr(path, mi.duration);
            } else {
                if (!loadStaticImageFromFile(srcPath, tImg, errMsg)) {
                    qWarning() << "Failed to load image:" << errMsg;
                    ImageDataService::instance()->addImage(srcPath, tImg);
                    DBManager::m_fileMutex.unlock();
                    continue;
                }
            }

            //裁切
            if (ImageDataService::instance()->getLoadMode() == 0) {
                qDebug() << "Clipping image to rect";
                tImg = clipToRect(tImg);
            } else if (ImageDataService::instance()->getLoadMode() == 1) {
                qDebug() << "Adding pad and scaling image";
                tImg = addPadAndScaled(tImg);
            }

            Libutils::base::mkMutiDir(thumbnailPath.mid(0, thumbnailPath.lastIndexOf('/')));
        }

        if (!tImg.isNull() && !thumbnailFile.exists()) {
            qDebug() << "Saving new thumbnail to:" << thumbnailPath;
            tImg.save(thumbnailPath, "PNG"); //保存裁好的缩略图，下次读的时候直接刷进去
        }

        ImageDataService::instance()->addImage(path, tImg);

        // 成功加载缩略图，通知上层界面刷新
        emit ImageDataService::instance()->gotImage(path);

        DBManager::m_fileMutex.unlock();
    }

    if (!stopFlag) {
        emit ImageDataService::instance()->sigeUpdateListview(); //最后让上层界面刷新
    }

    runningFlag = false; //告诉外面加载队列处于休眠状态
    qDebug() << "Thumbnail read process finished";
}

QImage ReadThumbnailManager::clipToRect(const QImage &src)
{
    auto tImg = src;

    if (!tImg.isNull() && 0 != tImg.height() && 0 != tImg.width() && (tImg.height() / tImg.width()) < 10 && (tImg.width() / tImg.height()) < 10) {
        bool cache_exist = false;
        if (tImg.height() != THUMBNAIL_MAX_SIZE && tImg.width() != THUMBNAIL_MAX_SIZE) {
            if (tImg.height() >= tImg.width()) {
                cache_exist = true;
                tImg = tImg.scaledToWidth(THUMBNAIL_MAX_SIZE,  Qt::FastTransformation);
            } else if (tImg.height() <= tImg.width()) {
                cache_exist = true;
                tImg = tImg.scaledToHeight(THUMBNAIL_MAX_SIZE,  Qt::FastTransformation);
            }
        }
        if (!cache_exist) {
            if ((static_cast<float>(tImg.height()) / (static_cast<float>(tImg.width()))) > 3) {
                tImg = tImg.scaledToWidth(THUMBNAIL_MAX_SIZE,  Qt::FastTransformation);
            } else {
                tImg = tImg.scaledToHeight(THUMBNAIL_MAX_SIZE,  Qt::FastTransformation);
            }
        }
    }

    if (!tImg.isNull()) {
        int width = tImg.width();
        int height = tImg.height();
        if (abs((width - height) * 10 / width) >= 1) {
            QRect rect = tImg.rect();
            int x = rect.x() + width / 2;
            int y = rect.y() + height / 2;
            if (width > height) {
                x = x - height / 2;
                y = 0;
                tImg = tImg.copy(x, y, height, height);
            } else {
                y = y - width / 2;
                x = 0;
                tImg = tImg.copy(x, y, width, width);
            }
        }
    }

    return tImg;
}

QImage ReadThumbnailManager::addPadAndScaled(const QImage &src)
{
    auto result = src.convertToFormat(QImage::Format_RGBA8888);

    if (result.height() > result.width()) {
        result = result.scaledToHeight(THUMBNAIL_MAX_SIZE, Qt::SmoothTransformation);
    } else {
        result = result.scaledToWidth(THUMBNAIL_MAX_SIZE, Qt::SmoothTransformation);
    }

    return result;
}
