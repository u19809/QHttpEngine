/*
 * Copyright (c) 2015 Nathan Osman
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QUrl>

#include "qfilesystemhandler.h"
#include "qfilesystemhandler_p.h"
#include "qiodevicecopier.h"

// Header for directory listings
const QString ListTemplateHeader =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>%1</title>"
        "</head><body><h1>%1</h1><p>Directory listing:</p><ul>";

// Footer for directory listings
const QString ListTemplateFooter =
        "</ul><hr><em>QHttpEngine %1</em></body></html>";

QFilesystemHandlerPrivate::QFilesystemHandlerPrivate(QFilesystemHandler *handler)
    : QObject(handler),
      q(handler)
{
}

bool QFilesystemHandlerPrivate::absolutePath(const QString &path, QString &absolutePath)
{
    // Clean the path and make it absolute
    absolutePath = QDir(documentRoot.absoluteFilePath(path)).canonicalPath();

    // Ensure that the absolute path is within the root
    return absolutePath.startsWith(documentRoot.canonicalPath());
}

QByteArray QFilesystemHandlerPrivate::mimeType(const QString &path)
{
    // TODO: use libmagic or the Windows registry when possible
    // TODO: determine what the equivalent is on OS X (libmagic?)

    QFileInfo info(path);
    QString extension = info.completeSuffix();

    if(extension == "htm" || extension == "html") { return "text/html"; }
    else if(extension == "css") { return "text/css"; }
    else if(extension == "js") { return "application/javascript"; }
    else if(extension == "jpg") { return "image/jpeg"; }
    else if(extension == "png") { return "image/png"; }
    else { return "application/octet-stream"; }
}

void QFilesystemHandlerPrivate::processFile(QHttpSocket *socket, const QString &absolutePath)
{
    // Attempt to open the file for reading
    QFile *file = new QFile(absolutePath);
    if(!file->open(QIODevice::ReadOnly)) {
        delete file;

        socket->writeError(QHttpSocket::Forbidden);
        return;
    }

    // Create a QIODeviceCopier to copy the file contents to the socket
    QIODeviceCopier *copier = new QIODeviceCopier(file, socket);
    connect(copier, SIGNAL(finished()), copier, SLOT(deleteLater()));
    connect(copier, SIGNAL(finished()), file, SLOT(deleteLater()));

    // Set the mimetype and content length
    socket->setHeader("Content-Type", mimeType(absolutePath));
    socket->setHeader("Content-Length", QByteArray::number(file->size()));
    socket->writeHeaders();

    // Start the copy
    copier->start();
}

void QFilesystemHandlerPrivate::processDirectory(QHttpSocket *socket, const QString &path, const QString &absolutePath)
{
    QString listing = ListTemplateHeader.arg(path.toHtmlEscaped());

    // Add entries for each of the files
    foreach(QFileInfo info, QDir(absolutePath).entryInfoList()) {
        listing.append(QString("<li><a href=\"%1%2\">%1%2</a></li>")
                .arg(info.fileName().toHtmlEscaped())
                .arg(info.isDir() ? "/" : ""));
    }

    listing.append(ListTemplateFooter.arg(QHTTPENGINE_VERSION));

    // Convert the string to UTF-8
    QByteArray data = listing.toUtf8();

    // Set the headers and write the content
    socket->setHeader("Content-Type", "text/html");
    socket->setHeader("Content-Length", QByteArray::number(data.length()));
    socket->write(data);
    socket->close();
}

QFilesystemHandler::QFilesystemHandler(QObject *parent)
    : QHttpHandler(parent),
      d(new QFilesystemHandlerPrivate(this))
{
}

QFilesystemHandler::QFilesystemHandler(const QString &documentRoot, QObject *parent)
    : QHttpHandler(parent),
      d(new QFilesystemHandlerPrivate(this))
{
    setDocumentRoot(documentRoot);
}

void QFilesystemHandler::process(QHttpSocket *socket, const QString &path)
{
    // If a document root is not set, an error has occurred
    if(d->documentRoot.path().isNull()) {
        socket->writeError(QHttpSocket::InternalServerError);
        return;
    }

    // URL-decode the path
    QString decodedPath = QUrl::fromPercentEncoding(path.toUtf8());

    // Attempt to retrieve the absolute path
    QString absolutePath;
    if(!d->absolutePath(decodedPath, absolutePath)) {
        socket->writeError(QHttpSocket::NotFound);
        return;
    }

    if(QFileInfo(absolutePath).isDir()) {
        d->processDirectory(socket, decodedPath, absolutePath);
    } else {
        d->processFile(socket, absolutePath);
    }
}

void QFilesystemHandler::setDocumentRoot(const QString &documentRoot)
{
    d->documentRoot.setPath(documentRoot);
}