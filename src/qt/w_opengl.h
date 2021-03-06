/*
 * 2018 Tarpeeksi Hyvae Soft /
 * VCS
 *
 */

#ifndef OGL_WIDGET_H
#define OGL_WIDGET_H

#include <QOpenGLFunctions_1_2>
#include <QOpenGLWidget>
#include <QWidget>
#include "../types.h"

class OGLWidget : public QOpenGLWidget, protected QOpenGLFunctions_1_2
{
    Q_OBJECT

public:
    explicit OGLWidget(QWidget *parent = 0);

protected:
    void initializeGL();
    void resizeGL(int w, int h);
    void paintGL();
};

#endif
