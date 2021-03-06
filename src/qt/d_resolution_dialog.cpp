/*
 * 2018 Tarpeeksi Hyvae Soft /
 * VCS resolution dialog
 *
 * A simple dialog that queries the user for a resolution and returns it.
 *
 */

#include "ui_d_resolution_dialog.h"
#include "d_resolution_dialog.h"
#include "../display.h"
#include "../capture.h"
#include "../common.h"

static resolution_s *RESOLUTION;

ResolutionDialog::ResolutionDialog(const QString title, resolution_s *const r, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ResolutionDialog)
{
    k_assert((r != nullptr),
             "Expected a valid width and height pointers in the resolution dialog, but got null.");

    ui->setupUi(this);

    RESOLUTION = r;

    setWindowTitle(title);

    const resolution_s &minres = kc_hardware_min_capture_resolution();
    const resolution_s &maxres = kc_hardware_max_capture_resolution();

    ui->spinBox_x->setMinimum(minres.w);
    ui->spinBox_x->setMaximum(maxres.w);
    ui->spinBox_y->setMinimum(minres.h);
    ui->spinBox_y->setMaximum(maxres.h);

    ui->label_minRes->setText(QString("Minimum: %1 x %2").arg(minres.w)
                                                         .arg(minres.h));
    ui->label_maxRes->setText(QString("Maximum: %1 x %2").arg(maxres.w)
                                                         .arg(maxres.h));

    ui->spinBox_x->setValue(RESOLUTION->w);
    ui->spinBox_y->setValue(RESOLUTION->h);

    // Don't show the context help '?' button in the window bar.
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    return;
}

ResolutionDialog::~ResolutionDialog()
{
    delete ui; ui = nullptr;
}

void ResolutionDialog::on_buttonBox_accepted()
{
    RESOLUTION->w = ui->spinBox_x->value();
    RESOLUTION->h = ui->spinBox_y->value();

    return;
}
