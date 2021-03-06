/*
 * 2018 Tarpeeksi Hyvae Soft /
 * VCS control panel
 *
 * The main UI dialog for controlling VCS. Orchestrates most other dialogs; sub-
 * ordinate only to the main (capture) window.
 *
 */

#include <QSignalMapper>
#include <QCloseEvent>
#include <QMessageBox>
#include <QFileDialog>
#include <QSettings>
#include <QDebug>
#include <QMenu>
#include <assert.h>
#include "d_filter_sets_list_dialog.h"
#include "d_video_and_color_dialog.h"
#include "../persistent_settings.h"
#include "d_resolution_dialog.h"
#include "ui_d_control_panel.h"
#include "d_anti_tear_dialog.h"
#include "d_control_panel.h"
#include "d_alias_dialog.h"
#include "../anti_tear.h"
#include "../capture.h"
#include "../display.h"
#include "../filter.h"
#include "../common.h"
#include "d_window.h"
#include "../main.h"
#include "../log.h"
#include "d_util.h"

static MainWindow *MAIN_WIN = nullptr;

ControlPanel::ControlPanel(MainWindow *const mainWin, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ControlPanel)
{
    MAIN_WIN = mainWin;
    k_assert(MAIN_WIN != nullptr,
             "Expected a valid main window pointer in the control panel, but got null.");

    ui->setupUi(this);

    setWindowTitle("VCS - Control Panel");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    aliasDlg = new AliasDialog;
    videocolorDlg = new VideoAndColorDialog;
    antitearDlg = new AntiTearDialog;
    filterSetsDlg = new FilterSetsListDialog;

    fill_hardware_info_table();
    update_input_signal_info(kc_input_signal_info());
    connect_input_resolution_buttons();
    fill_output_scaling_filter_comboboxes();
    fill_input_channel_combobox();
    reset_capture_bit_depth_combobox();

    // Adjust sundry GUI controls to their proper values.
    {
        const resolution_s min = kc_hardware_min_capture_resolution();
        const resolution_s max = kc_hardware_max_capture_resolution();
        ui->spinBox_outputResX->setMinimum(min.w);
        ui->spinBox_outputResY->setMinimum(min.h);
        ui->spinBox_outputResX->setMaximum(max.w);
        ui->spinBox_outputResY->setMaximum(max.h);

        ui->spinBox_outputPadX->setMinimum(MIN_OUTPUT_WIDTH);
        ui->spinBox_outputPadY->setMinimum(MIN_OUTPUT_HEIGHT);

        ui->treeWidget_logList->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

        resolution_s r;
        r.w = ui->spinBox_outputAspectX->value();
        r.h = ui->spinBox_outputAspectY->value();
        ks_set_output_aspect_ratio(r.w, r.h);

        ui->groupBox_aboutVCS->setTitle("VCS v" + QString("%1").arg(PROGRAM_VERSION_STRING));
    }

    // Restore persistent settings.
    {
        ui->checkBox_logEnabled->setChecked(kpers_value_of("enabled", INI_GROUP_LOG, 1).toBool());
        ui->tabWidget->setCurrentIndex(kpers_value_of("tab", INI_GROUP_CONTROL_PANEL, 0).toUInt());
        ui->checkBox_customFiltering->setChecked(kpers_value_of("custom_filtering", INI_GROUP_OUTPUT_FILTERS, 0).toBool());
        ui->checkBox_outputAntiTear->setChecked(kpers_value_of("enabled", INI_GROUP_ANTI_TEAR, 0).toBool());

        resize(kpers_value_of("control_panel", INI_GROUP_GEOMETRY, size()).toSize());
    }

    return;
}

ControlPanel::~ControlPanel()
{
    // Save the current settings.
    kpers_set_value("upscaler", INI_GROUP_OUTPUT_FILTERS, ui->comboBox_outputUpscaleFilter->currentText());
    kpers_set_value("downscaler", INI_GROUP_OUTPUT_FILTERS, ui->comboBox_outputDownscaleFilter->currentText());
    kpers_set_value("enabled", INI_GROUP_LOG, ui->checkBox_logEnabled->isChecked());
    kpers_set_value("custom_filtering", INI_GROUP_OUTPUT_FILTERS, ui->checkBox_customFiltering->isChecked());
    kpers_set_value("enabled", INI_GROUP_ANTI_TEAR, ui->checkBox_outputAntiTear->isChecked());
    kpers_set_value("tab", INI_GROUP_CONTROL_PANEL, ui->tabWidget->currentIndex());
    kpers_set_value("control_panel", INI_GROUP_GEOMETRY, size());

    delete ui; ui = nullptr;
    delete aliasDlg; aliasDlg = nullptr;
    delete videocolorDlg; videocolorDlg = nullptr;
    delete antitearDlg; antitearDlg = nullptr;
    delete filterSetsDlg; filterSetsDlg = nullptr;

    return;
}

void ControlPanel::keyPressEvent(QKeyEvent *event)
{
    // Don't allow ESC to close the control panel.
    if (event->key() == Qt::Key_Escape)
    {
        event->ignore();
    }
    else
    {
        QDialog::keyPressEvent(event);
    }

    return;
}

void ControlPanel::closeEvent(QCloseEvent *event)
{
    // Don't allow the control panel to be closed unless the entire program is
    // closing.
    if (!PROGRAM_EXIT_REQUESTED)
    {
        event->ignore();
    }
    else
    {
        k_assert(videocolorDlg != nullptr, "");
        videocolorDlg->close();

        k_assert(aliasDlg != nullptr, "");
        aliasDlg->close();

        k_assert(antitearDlg != nullptr, "");
        antitearDlg->close();

        k_assert(filterSetsDlg != nullptr, "");
        filterSetsDlg->close();
    }

    return;
}

void ControlPanel::notify_of_new_alias(const mode_alias_s a)
{
    k_assert(aliasDlg != nullptr, "");
    aliasDlg->receive_new_alias(a);

    return;
}

void ControlPanel::notify_of_new_mode_settings_source_file(const QString &filename)
{
    k_assert(videocolorDlg != nullptr, "");
    videocolorDlg->receive_new_mode_settings_filename(filename);
}

void ControlPanel::fill_input_channel_combobox()
{
    block_widget_signals_c b(ui->comboBox_inputChannel);

    ui->comboBox_inputChannel->clear();

    for (uint i = 0; i < kc_hardware_num_capture_inputs(); i++)
    {
        ui->comboBox_inputChannel->addItem(QString("Channel #%1").arg((i + 1)));
    }

    ui->comboBox_inputChannel->setCurrentIndex(INPUT_CHANNEL_IDX);

    // Lock the input channel selector if only one channel is available.
    if (ui->comboBox_inputChannel->count() == 1)
    {
        ui->comboBox_inputChannel->setEnabled(false);
    }

    return;
}

// Adds the names of the available scaling filters to the GUI's list.
//
void ControlPanel::fill_output_scaling_filter_comboboxes()
{
    QString defaultUpscaleFilt = kpers_value_of("upscaler", INI_GROUP_OUTPUT_FILTERS, "Linear").toString();
    QString defaultDownscaleFilt = kpers_value_of("downscaler", INI_GROUP_OUTPUT_FILTERS, "Linear").toString();

    const QStringList filters = ks_list_of_scaling_filter_names();
    k_assert(!filters.isEmpty(),
             "Expected to receive a list of scaling filters, but got an empty list.");

    block_widget_signals_c b1(ui->comboBox_outputUpscaleFilter);
    block_widget_signals_c b2(ui->comboBox_outputDownscaleFilter);

    ui->comboBox_outputUpscaleFilter->clear();
    ui->comboBox_outputDownscaleFilter->clear();

    for (int i = 0; i < filters.size(); i++)
    {
        ui->comboBox_outputUpscaleFilter->addItem(filters[i]);
        ui->comboBox_outputDownscaleFilter->addItem(filters[i]);
    }

    auto confirm_index_is_valid = [](int &idx, const char *const scalerType)
                                  {
                                      if (idx == -1)
                                      {
                                          NBENE(("Unable to change the default %s filter: filter not found. "
                                                 "Defaulting to the first available filter.", scalerType));
                                          idx = 0;
                                      }
                                  };

    int idx = ui->comboBox_outputUpscaleFilter->findText(defaultUpscaleFilt, Qt::MatchExactly);
    confirm_index_is_valid(idx, "upscaling");
    ui->comboBox_outputUpscaleFilter->setCurrentIndex(idx);
    ks_set_upscaling_filter(ui->comboBox_outputUpscaleFilter->itemText(idx));

    idx = ui->comboBox_outputDownscaleFilter->findText(defaultDownscaleFilt, Qt::MatchExactly);
    confirm_index_is_valid(idx, "downscaling");
    ui->comboBox_outputDownscaleFilter->setCurrentIndex(idx);
    ks_set_downscaling_filter(ui->comboBox_outputDownscaleFilter->itemText(idx));

    return;
}

void ControlPanel::update_output_framerate(const uint fps,
                                           const bool hasMissedFrames)
{
    if (kc_is_no_signal())
    {
        DEBUG(("Was asked to update GUI output info while there was no signal."));
    }
    else
    {
        ui->label_outputFPS->setText((fps == 0)? "Unknown" : QString("%1").arg(fps));
        ui->label_outputProcessTime->setText(hasMissedFrames? "Dropping frames" : "No problem");
    }

    return;
}

void ControlPanel::set_input_info_as_no_signal()
{
    ui->label_captInputResolution->setText("n/a");
    ui->label_outputInputResolution->setText("n/a");

    if (kc_is_invalid_signal())
    {
        ui->label_captInputSignal->setText("Invalid signal");
    }
    else
    {
        ui->label_captInputSignal->setText("No signal");
    }

    set_input_controls_enabled(false);
    set_output_controls_enabled(false);

    k_assert(videocolorDlg != nullptr, "");
    videocolorDlg->set_controls_enabled(false);

    return;
}

void ControlPanel::set_input_info_as_receiving_signal()
{
    set_input_controls_enabled(true);
    set_output_controls_enabled(true);

    k_assert(videocolorDlg != nullptr, "");
    videocolorDlg->set_controls_enabled(true);

    return;
}

void ControlPanel::set_input_controls_enabled(const bool state)
{
    ui->frame_inputForceButtons->setEnabled(state);
    ui->pushButton_inputAliases->setEnabled(state);
    ui->pushButton_inputAdjustVideoColor->setEnabled(state);
    ui->comboBox_frameSkip->setEnabled(state);
    ui->comboBox_bitDepth->setEnabled(state);
    ui->label_captInputResolution->setEnabled(state);

    return;
}

// Sets the output tab's control buttons etc. as enabled or disabled.
//
void ControlPanel::set_output_controls_enabled(const bool state)
{
    ui->label_outputFPS->setEnabled(state);
    ui->label_outputInputResolution->setEnabled(state);
    ui->label_outputProcessTime->setEnabled(state);

    ui->label_outputFPS->setText("n/a");
    ui->label_outputProcessTime->setText("n/a");
    ui->label_outputInputResolution->setText("n/a");

    return;
}

// Signal to the control panel that it should update itself on the current output
// resolution.
//
void ControlPanel::update_output_resolution_info(void)
{
    const resolution_s r = ks_padded_output_resolution();

    ui->label_outputResolution->setText(QString("%1 x %2").arg(r.w).arg(r.h));
    if (r.w == MIN_OUTPUT_WIDTH ||
        r.h == MIN_OUTPUT_HEIGHT ||
        r.w == MAX_OUTPUT_WIDTH ||
        r.h == MAX_OUTPUT_HEIGHT)
    {
        // Assume the resolution matches the limits because it was forced as such,
        // due to being too large/small otherwise.
        ui->label_outputResolution->setText(
                    ui->label_outputResolution->text() + " (limited)");
    }

    // If the user isn't forcing an aspect ratio, we can auto-update the values to
    // match the current output resolution's ratio.
    if (!ui->checkBox_forceOutputAspect->isChecked())
    {
        // Note: We use the version of the output resolution that excludes any
        // padding, since we want the aspect ratio to reflect the size of the
        // actual captured frame and not that of the padded output.
        const resolution_s a = ks_resolution_to_aspect_ratio(ks_output_resolution());

        ui->spinBox_outputAspectX->setValue(a.w);
        ui->spinBox_outputAspectY->setValue(a.h);
    }

    return;
}

void ControlPanel::update_current_filter_set_idx(const int idx)
{
    k_assert(filterSetsDlg != nullptr, "");
    filterSetsDlg->receive_current_filter_set_idx(idx);

    return;
}

void ControlPanel::update_input_signal_info(const input_signal_s &s)
{
    if (kc_is_no_signal())
    {
        DEBUG(("Was asked to update GUI input info while there was no signal."));
    }
    else
    {
        k_assert(videocolorDlg != nullptr, "");
        videocolorDlg->receive_new_input_signal(s);

        // Mark the resolution. If either dimenson is 0, we expect it to be an
        // invalid reading that should be ignored.
        if (s.r.w == 0 || s.r.h == 0)
        {
            ui->label_captInputResolution->setText("n/a");
            ui->label_outputInputResolution->setText("n/a");
        }
        else
        {
            QString res = QString("%1 x %2").arg(s.r.w).arg(s.r.h);

            if (kc_is_aliased_resolution())
            {
                res += " (a)";
            }

            ui->label_captInputResolution->setText(res);
            ui->label_outputInputResolution->setText(res);
        }

        // Mark the refresh rate. If the value is 0, we expect it to be an invalid
        // reading and that we should ignore it.
        if (s.refreshRate != 0)
        {
            const QString t = ui->label_captInputResolution->text();

            ui->label_captInputResolution->setText(t + QString(", %1 Hz").arg(s.refreshRate));
        }

        ui->label_captInputSignal->setText(s.isDigital? "Digital" : "Analog");

        if (!ui->checkBox_forceOutputRes->isChecked())
        {
            ui->spinBox_outputResX->setValue(s.r.w);
            ui->spinBox_outputResY->setValue(s.r.h);
        }
    }

    return;
}

void ControlPanel::clear_known_aliases()
{
    k_assert(aliasDlg != nullptr, "");
    aliasDlg->clear_known_aliases();

    return;
}

void ControlPanel::clear_known_modes()
{
    k_assert(videocolorDlg != nullptr, "");
    videocolorDlg->clear_known_modes();

    return;
}

// Sets up the buttons for forcing output resolution.
//
void ControlPanel::connect_input_resolution_buttons()
{
    QSignalMapper *sm = new QSignalMapper(this);

    for (int i = 0; i < ui->frame_inputForceButtons->layout()->count(); i++)
    {
        QWidget *const w = ui->frame_inputForceButtons->layout()->itemAt(i)->widget();

        k_assert(w->objectName().contains("pushButton"),
                 "Expected all widgets in this layout to be pushbuttons.");

        // Store the unique id of this button, so we can later identify it.
        ((QPushButton*)w)->setProperty("butt_id", i);

        // Load in any custom resolutions the user may have set earlier.
        if (kpers_contains(QString("butt_%1").arg(i), INI_GROUP_FORCE_INPUT_BUTTONS))
        {
            ((QPushButton*)w)->setText(kpers_value_of(QString("butt_%1").arg(i), INI_GROUP_FORCE_INPUT_BUTTONS).toString());
        }

        connect(w, SIGNAL(clicked()), sm, SLOT(map()));
        sm->setMapping(w, w);
    }

    connect(sm, SIGNAL(mapped(QWidget*)), this, SLOT(parse_input_resolution_button_press(QWidget*)));

    return;
}

// Gets called when a button for forcing the input resolution is pressed in the GUI.
// Will then decide which input resolution to force based on which button was pressed.
//
void ControlPanel::parse_input_resolution_button_press(QWidget *button)
{
    QStringList sl;
    resolution_s res = {0, 0};

    k_assert(button->objectName().contains("pushButton"),
             "Expected a button widget, but received something else.");

    // Query the user for a custom resolution.
    /// TODO. Get a more reliable way.
    if (((QPushButton*)button)->text() == "Other...")
    {
        res.w = 1920;
        res.h = 1080;
        if (ResolutionDialog("Force an input resolution",
                             &res, parentWidget()).exec() == QDialog::Rejected)
        {
            // If the user canceled.
            goto done;
        }

        goto assign_resolution;
    }

    // Extract the resolution from the button name. The name is expected to be
    // of the form e.g. '640 x 480' or '640x480'.
    sl = ((QPushButton*)button)->text().split('x');
    if (sl.size() < 2)
    {
        DEBUG(("Unexpected number of parameters in a button name. Expected at least width and height."));
        goto done;
    }
    else
    {
        res.w = sl.at(0).toUInt();
        res.h = sl.at(1).toUInt();
    }

    // If ctrl is pressed while clicking the button, let the user specify a new
    // resolution for the button.
    if (QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
    {
        // Pop up a dialog asking the user for the new resolution.
        if (ResolutionDialog("Assign an input resolution",
                             &res, parentWidget()).exec() != QDialog::Rejected)
        {
            const QString resolutionStr = QString("%1 x %2").arg(res.w).arg(res.h);

            ((QPushButton*)button)->setText(resolutionStr);

            // Save the new resolution into the ini.
            kpers_set_value(QString("butt_%1").arg(((QPushButton*)button)->property("butt_id").toUInt()),
                            INI_GROUP_FORCE_INPUT_BUTTONS, resolutionStr);

            DEBUG(("Assigned a new resolution (%u x %u) for an input force button.",
                   res.w, res.h));
        }

        goto done;
    }

    assign_resolution:
    DEBUG(("Received a request via the GUI to set the input resolution to %u x %u.", res.w, res.h));
    kmain_change_capture_input_resolution(res);

    done:
    return;
}

// Polls the capture hardware for information about itself, and puts that info
// in the GUI for perusal.
//
void ControlPanel::fill_hardware_info_table()
{
    QString s;

    // Get the capture card model name.
    ui->groupBox_captureDeviceInfo->setTitle("Capture device: " + kc_capture_card_type_string());

    const resolution_s &minres = kc_hardware_min_capture_resolution();
    const resolution_s &maxres = kc_hardware_max_capture_resolution();

    // Get the minimum and maximum resolutions;
    s = QString("%1 x %2").arg(minres.w).arg(minres.h);
    ui->label_inputMinResolutionString->setText(s);
    s = QString("%1 x %2").arg(maxres.w).arg(maxres.h);
    ui->label_inputMaxResolutionString->setText(s);

    // Number of input channels.
    s = QString::number(kc_hardware_num_capture_inputs());
    ui->label_inputChannelsString->setText(s);

    // Firmware and driver versions.
    ui->label_firmwareString->setText(kc_hardware_firmware_version_string());
    ui->label_driverString->setText(kc_hardware_driver_version_string());

    // Support matrix for various features.
    ui->label_supportsDirectDMAString->setText(kc_hardware_is_direct_dma_supported()? "Yes" : "No");
    ui->label_supportsDeinterlaceString->setText(kc_hardware_is_deinterlace_supported()? "Yes" : "No");
    ui->label_supportsYUVString->setText(kc_hardware_is_yuv_supported()? "Yes" : "No");
    ui->label_supportsVGACaptureString->setText(kc_hardware_is_vga_capture_supported()? "Yes" : "No");
    ui->label_supportsDVICaptureString->setText(kc_hardware_is_dvi_capture_supported()? "Yes" : "No");
    ui->label_supportsCompositeCaptureString->setText(kc_hardware_is_composite_capture_supported()? "Yes" : "No");
    ui->label_supportsComponentCaptureString->setText(kc_hardware_is_component_capture_supported()? "Yes" : "No");
    ui->label_supportsSVideoCaptureString->setText(kc_hardware_is_svideo_capture_supported()? "Yes" : "No");

    return;
}

void ControlPanel::add_gui_log_entry(const log_entry_s &e)
{
    // Sanity check, to make sure we've set up the GUI correctly.
    k_assert(ui->treeWidget_logList->columnCount() == 3,
             "Expected the log list to have three columns.");

    QTreeWidgetItem *entry = new QTreeWidgetItem;

    entry->setText(0, e.date);
    entry->setText(1, e.type);
    entry->setText(2, e.message);

    ui->treeWidget_logList->addTopLevelItem(entry);

    filter_log_entry(entry);

    return;
}

// Initializes the visibility of the given entry based on whether the user has
// selected to show/hide entries of its kind.
//
void ControlPanel::filter_log_entry(QTreeWidgetItem *const entry)
{
    const int typeColumn = 1;  // The column index in the tree that gives the log entry's type.

    entry->setHidden(true);

    if (ui->checkBox_logInfo->isChecked() &&
        entry->text(typeColumn) == "Info")
    {
        entry->setHidden(false);
    }

    if (ui->checkBox_logDebug->isChecked() &&
        entry->text(typeColumn) == "Debug")
    {
        entry->setHidden(false);
    }

    if (ui->checkBox_logErrors->isChecked() &&
        entry->text(typeColumn) == "N.B.")
    {
        entry->setHidden(false);
    }

    return;
}

void ControlPanel::refresh_log_list_filtering()
{
    const int typeColumn = 1;  // The column index in the tree that gives the log entry's type.
    QList<QTreeWidgetItem*> entries = ui->treeWidget_logList->findItems("*", Qt::MatchWildcard, typeColumn);

    for (auto *entry: entries)
    {
        filter_log_entry(entry);
    }

    return;
}

// Adjusts the output scale value in the GUI by a pre-set step size in the given
// direction. Note that this will automatically trigger a change in the actual
// scaler output size as well.
//
void ControlPanel::adjust_output_scaling(const int dir)
{
    const int curValue = ui->spinBox_outputScale->value();
    const int stepSize = ui->spinBox_outputScale->singleStep();

    k_assert((dir == 1 || dir == -1),
             "Expected the parameter for AdjustOutputScaleValue to be either 1 or -1.");

    if (!ui->checkBox_forceOutputScale->isChecked())
    {
        ui->checkBox_forceOutputScale->setChecked(true);
    }

    ui->spinBox_outputScale->setValue(curValue + (stepSize * dir));

    return;
}

void ControlPanel::set_overlay_indicator_checked(const bool checked)
{
    block_widget_signals_c b(ui->checkBox_overlay);

    ui->checkBox_overlay->setChecked(checked);

    return;
}

// Queries the current capture input bit depth and sets the GUI combobox selection
// accordingly.
//
void ControlPanel::reset_capture_bit_depth_combobox()
{
    QString depthString = QString("%1-bit").arg(kc_output_bit_depth()); // E.g. "24-bit".

    for (int i = 0; i < ui->comboBox_bitDepth->count(); i++)
    {
        if (ui->comboBox_bitDepth->itemText(i).contains(depthString))
        {
            ui->comboBox_bitDepth->setCurrentIndex(i);
            goto done;
        }
    }

    k_assert(0, "Failed to set up the GUI for the current capture bit depth.");

    done:
    return;
}

void ControlPanel::on_checkBox_forceOutputScale_stateChanged(int arg1)
{
    const bool checked = (arg1 == Qt::Checked)? 1 : 0;

    k_assert(arg1 != Qt::PartiallyChecked,
             "Expected a two-state toggle for 'forceOutputScale'. It appears to have a third state.");

    ui->spinBox_outputScale->setEnabled(checked);

    if (checked)
    {
        const int s = ui->spinBox_outputScale->value();

        ks_set_output_scaling(s / 100.0);
    }
    else
    {
        ui->spinBox_outputScale->setValue(100);
    }

    ks_set_output_scale_override_enabled(checked);

    return;
}

void ControlPanel::on_checkBox_forceOutputAspect_stateChanged(int arg1)
{
    const bool checked = (arg1 == Qt::Checked)? 1 : 0;

    k_assert(arg1 != Qt::PartiallyChecked,
             "Expected a two-state toggle for 'forceOutputAspect'. It appears to have a third state.");

    ui->spinBox_outputAspectX->setEnabled(checked);
    ui->spinBox_outputAspectY->setEnabled(checked);
    ui->label_outputAspectSeparator->setEnabled(checked);

    if (checked)
    {
        const int w = ui->spinBox_outputAspectX->value();
        const int h = ui->spinBox_outputAspectY->value();

        ks_set_output_aspect_ratio(w, h);
    }
    else
    {
        const resolution_s a = ks_resolution_to_aspect_ratio({(uint)ui->spinBox_outputResX->value(),
                                                              (uint)ui->spinBox_outputResY->value(), 0});

        ui->spinBox_outputAspectX->setValue(a.w);
        ui->spinBox_outputAspectY->setValue(a.h);

        ks_set_output_aspect_ratio(a.w, a.h);
    }

    ks_set_output_aspect_ratio_override_enabled(checked);

    return;
}

void ControlPanel::on_checkBox_forceOutputPad_stateChanged(int arg1)
{
    const bool checked = (arg1 == Qt::Checked)? 1 : 0;

    k_assert(arg1 != Qt::PartiallyChecked,
             "Expected a two-state toggle for 'outputPad'. It appears to have a third state.");

    ui->spinBox_outputPadX->setEnabled(checked);
    ui->spinBox_outputPadY->setEnabled(checked);
    ui->label_outputPadSeparator->setEnabled(checked);

    ks_set_output_pad_override_enabled(checked);

    return;
}

void ControlPanel::on_checkBox_forceOutputRes_stateChanged(int arg1)
{
    const bool checked = (arg1 == Qt::Checked)? 1 : 0;

    k_assert(arg1 != Qt::PartiallyChecked,
             "Expected a two-state toggle for 'outputRes'. It appears to have a third state.");

    ui->spinBox_outputResX->setEnabled(checked);
    ui->spinBox_outputResY->setEnabled(checked);
    ui->label_outputBaseResSeparator->setEnabled(checked);

    ks_set_output_resolution_override_enabled(checked);

    if (!checked)
    {
        resolution_s outr;
        const resolution_s r = kc_input_signal_info().r;

        ks_set_output_base_resolution(r, true);

        outr = ks_output_base_resolution();
        ui->spinBox_outputResX->setValue(outr.w);
        ui->spinBox_outputResY->setValue(outr.h);
    }

    return;
}

void ControlPanel::on_comboBox_frameSkip_currentIndexChanged(const QString &arg1)
{
    int v = 0;

    if (arg1 == "None")
    {
        v = 0;
    }
    else if (arg1 == "Half")
    {
        v = 1;
    }
    else if (arg1 == "Two thirds")
    {
        v = 2;
    }
    else if (arg1 == "Three quarters")
    {
        v = 3;
    }
    else
    {
        k_assert(0, "Unexpected GUI string for frame-skipping.");
    }

    kc_set_capture_frame_dropping(v);

    return;
}

void ControlPanel::on_comboBox_outputUpscaleFilter_currentIndexChanged(const QString &arg1)
{
    ks_set_upscaling_filter(arg1);

    return;
}

void ControlPanel::on_comboBox_outputDownscaleFilter_currentIndexChanged(const QString &arg1)
{
    ks_set_downscaling_filter(arg1);

    return;
}

void ControlPanel::on_checkBox_logEnabled_stateChanged(int arg1)
{
    k_assert(arg1 != Qt::PartiallyChecked,
             "Expected a two-state toggle for 'enableLogging'. It appears to have a third state.");

    klog_set_logging_enabled(arg1);

    ui->treeWidget_logList->setEnabled(arg1);

    return;
}

void ControlPanel::on_spinBox_outputScale_valueChanged(int)
{
    real scale = ui->spinBox_outputScale->value() / 100.0;

    ks_set_output_scaling(scale);

    return;
}

void ControlPanel::on_spinBox_outputAspectX_valueChanged(int)
{
    const u32 w = ui->spinBox_outputAspectX->value();
    const u32 h = ui->spinBox_outputAspectY->value();

    if (!ui->checkBox_forceOutputAspect->isChecked())
    {
        return;
    }

    ks_set_output_aspect_ratio(w, h);

    return;
}

void ControlPanel::on_spinBox_outputAspectY_valueChanged(int)
{
    const u32 w = ui->spinBox_outputAspectX->value();
    const u32 h = ui->spinBox_outputAspectY->value();

    if (!ui->checkBox_forceOutputAspect->isChecked())
    {
        return;
    }

    ks_set_output_aspect_ratio(w, h);

    return;
}

void ControlPanel::on_spinBox_outputPadX_valueChanged(int)
{
    const resolution_s r = {(uint)ui->spinBox_outputPadX->value(),
                            (uint)ui->spinBox_outputPadY->value(), 0};

    if (!ui->checkBox_forceOutputPad->isChecked())
    {
        return;
    }

    ks_set_output_pad_resolution(r);

    return;
}

void ControlPanel::on_spinBox_outputPadY_valueChanged(int)
{
    const resolution_s r = {(uint)ui->spinBox_outputPadX->value(),
                            (uint)ui->spinBox_outputPadY->value(), 0};

    if (!ui->checkBox_forceOutputPad->isChecked())
    {
        return;
    }

    ks_set_output_pad_resolution(r);

    return;
}

void ControlPanel::on_spinBox_outputResX_valueChanged(int)
{
    const resolution_s r = {(uint)ui->spinBox_outputResX->value(),
                            (uint)ui->spinBox_outputResY->value(), 0};

    if (!ui->checkBox_forceOutputRes->isChecked())
    {
        return;
    }

    ks_set_output_base_resolution(r, true);

    return;
}

void ControlPanel::on_spinBox_outputResY_valueChanged(int)
{
    const resolution_s r = {(uint)ui->spinBox_outputResX->value(),
                            (uint)ui->spinBox_outputResY->value(), 0};

    if (!ui->checkBox_forceOutputRes->isChecked())
    {
        return;
    }

    ks_set_output_base_resolution(r, true);

    return;
}

void ControlPanel::on_pushButton_inputAdjustVideoColor_clicked()
{
    k_assert(videocolorDlg != nullptr, "");
    videocolorDlg->show();
    videocolorDlg->activateWindow();
    videocolorDlg->raise();

    return;
}

void ControlPanel::on_comboBox_inputChannel_currentIndexChanged(int index)
{
    // If we fail to set the input channel, revert back to the current one.
    if (!kc_set_capture_input_channel(index))
    {
        block_widget_signals_c b(ui->comboBox_inputChannel);

        NBENE(("Failed to set the input channel to %d. Reverting.", index));
        ui->comboBox_inputChannel->setCurrentIndex(kc_input_channel_idx());
    }

    return;
}

void ControlPanel::on_pushButton_inputAliases_clicked()
{
    k_assert(aliasDlg != nullptr, "");
    aliasDlg->show();
    aliasDlg->activateWindow();
    aliasDlg->raise();

    return;
}

void ControlPanel::on_pushButton_editOverlay_clicked()
{
    k_assert(MAIN_WIN != nullptr, "");
    MAIN_WIN->show_overlay_dialog();

    return;
}

void ControlPanel::on_checkBox_overlay_stateChanged(int arg1)
{
    const bool checked = (arg1 == Qt::Checked)? 1 : 0;

    k_assert(MAIN_WIN != nullptr, "");
    MAIN_WIN->set_overlay_enabled(checked);

    return;
}

void ControlPanel::on_comboBox_bitDepth_currentIndexChanged(const QString &arg1)
{
    u32 bpp = 0;

    if (arg1.contains("24-bit"))
    {
        bpp = 24;
    }
    else if (arg1.contains("16-bit"))
    {
        bpp = 16;
    }
    else if (arg1.contains("15-bit"))
    {
        bpp = 15;
    }
    else
    {
        k_assert(0, "Unrecognized color depth option in the GUI dropbox.");
    }

    if (!kc_set_output_bit_depth(bpp))
    {
        reset_capture_bit_depth_combobox();

        done_reset:
        kd_show_headless_error_message("", "Failed to change the capture color depth.\n\n"
                                           "The previous setting has been restored.");
    }

    return;
}

void ControlPanel::on_checkBox_outputAntiTear_stateChanged(int arg1)
{
    const bool checked = (arg1 == Qt::Checked)? 1 : 0;

    k_assert(arg1 != Qt::PartiallyChecked,
             "Expected a two-state toggle for 'outputAntiTear'. It appears to have a third state.");

    kat_set_anti_tear_enabled(checked);

    return;
}

void ControlPanel::on_pushButton_antiTearOptions_clicked()
{
    k_assert(antitearDlg != nullptr, "");
    antitearDlg->show();
    antitearDlg->activateWindow();
    antitearDlg->raise();

    return;
}

void ControlPanel::on_pushButton_configureFiltering_clicked()
{
    k_assert(filterSetsDlg != nullptr, "");
    filterSetsDlg->show();
    filterSetsDlg->activateWindow();
    filterSetsDlg->raise();

    return;
}

void ControlPanel::on_checkBox_customFiltering_stateChanged(int arg1)
{
    const bool checked = (arg1 == Qt::Checked)? 1 : 0;

    k_assert(arg1 != Qt::PartiallyChecked,
             "Expected a two-state toggle for 'customFiltering'. It appears to have a third state.");

    kf_set_filtering_enabled(checked);

    k_assert(filterSetsDlg != nullptr, "");
    filterSetsDlg->signal_filtering_enabled(checked);

    return;
}

void ControlPanel::on_checkBox_logInfo_toggled(bool)
{
    refresh_log_list_filtering();

    return;
}

void ControlPanel::on_checkBox_logDebug_toggled(bool)
{
    refresh_log_list_filtering();

    return;
}

void ControlPanel::on_checkBox_logErrors_toggled(bool)
{
    refresh_log_list_filtering();

    return;
}

QString ControlPanel::GetString_OutputResolution()
{
    return ui->label_outputResolution->text();
}

QString ControlPanel::GetString_InputResolution()
{
    const resolution_s r = kc_input_resolution();

    return QString("%1 x %2").arg(r.w).arg(r.h);
}

QString ControlPanel::GetString_InputRefreshRate()
{
    return QString("%1 Hz").arg(kc_input_signal_info().refreshRate);
}

QString ControlPanel::GetString_OutputFrameRate()
{
    return ui->label_outputFPS->text();
}

QString ControlPanel::GetString_DroppingFrames()
{
    return ui->label_outputProcessTime->text();
}

QString ControlPanel::GetString_OutputLatency()
{
    return ui->label_outputProcessTime->text();
}

bool ControlPanel::is_mouse_wheel_scaling_allowed()
{
    return ui->checkBox_outputAllowMouseWheelScale->isChecked();
}
