// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <thread>

#include "common/assert.h"
#include "common/param_package.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/hid/emulated_controller.h"
#include "core/hid/hid_core.h"
#include "core/hid/hid_types.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/sm/sm.h"
#include "ui_qt_controller.h"
#include "yuzu/applets/qt_controller.h"
#include "yuzu/configuration/configure_input.h"
#include "yuzu/configuration/configure_input_profile_dialog.h"
#include "yuzu/configuration/configure_motion_touch.h"
#include "yuzu/configuration/configure_vibration.h"
#include "yuzu/configuration/input_profiles.h"
#include "yuzu/main.h"

namespace {

void UpdateController(Core::HID::EmulatedController* controller,
                      Core::HID::NpadStyleIndex controller_type, bool connected) {
    if (controller->IsConnected(true)) {
        controller->Disconnect();
    }
    controller->SetNpadStyleIndex(controller_type);
    if (connected) {
        controller->Connect(true);
    }
}

// Returns true if the given controller type is compatible with the given parameters.
bool IsControllerCompatible(Core::HID::NpadStyleIndex controller_type,
                            Core::Frontend::ControllerParameters parameters) {
    switch (controller_type) {
    case Core::HID::NpadStyleIndex::ProController:
        return parameters.allow_pro_controller;
    case Core::HID::NpadStyleIndex::JoyconDual:
        return parameters.allow_dual_joycons;
    case Core::HID::NpadStyleIndex::JoyconLeft:
        return parameters.allow_left_joycon;
    case Core::HID::NpadStyleIndex::JoyconRight:
        return parameters.allow_right_joycon;
    case Core::HID::NpadStyleIndex::Handheld:
        return parameters.enable_single_mode && parameters.allow_handheld;
    case Core::HID::NpadStyleIndex::GameCube:
        return parameters.allow_gamecube_controller;
    default:
        return false;
    }
}

} // namespace

QtControllerSelectorDialog::QtControllerSelectorDialog(
    QWidget* parent, Core::Frontend::ControllerParameters parameters_,
    InputCommon::InputSubsystem* input_subsystem_, Core::System& system_)
    : QDialog(parent), ui(std::make_unique<Ui::QtControllerSelectorDialog>()),
      parameters(std::move(parameters_)), input_subsystem{input_subsystem_},
      input_profiles(std::make_unique<InputProfiles>(system_)), system{system_} {
    ui->setupUi(this);

    player_widgets = {
        ui->widgetPlayer1, ui->widgetPlayer2, ui->widgetPlayer3, ui->widgetPlayer4,
        ui->widgetPlayer5, ui->widgetPlayer6, ui->widgetPlayer7, ui->widgetPlayer8,
    };

    player_groupboxes = {
        ui->groupPlayer1Connected, ui->groupPlayer2Connected, ui->groupPlayer3Connected,
        ui->groupPlayer4Connected, ui->groupPlayer5Connected, ui->groupPlayer6Connected,
        ui->groupPlayer7Connected, ui->groupPlayer8Connected,
    };

    connected_controller_icons = {
        ui->controllerPlayer1, ui->controllerPlayer2, ui->controllerPlayer3, ui->controllerPlayer4,
        ui->controllerPlayer5, ui->controllerPlayer6, ui->controllerPlayer7, ui->controllerPlayer8,
    };

    led_patterns_boxes = {{
        {ui->checkboxPlayer1LED1, ui->checkboxPlayer1LED2, ui->checkboxPlayer1LED3,
         ui->checkboxPlayer1LED4},
        {ui->checkboxPlayer2LED1, ui->checkboxPlayer2LED2, ui->checkboxPlayer2LED3,
         ui->checkboxPlayer2LED4},
        {ui->checkboxPlayer3LED1, ui->checkboxPlayer3LED2, ui->checkboxPlayer3LED3,
         ui->checkboxPlayer3LED4},
        {ui->checkboxPlayer4LED1, ui->checkboxPlayer4LED2, ui->checkboxPlayer4LED3,
         ui->checkboxPlayer4LED4},
        {ui->checkboxPlayer5LED1, ui->checkboxPlayer5LED2, ui->checkboxPlayer5LED3,
         ui->checkboxPlayer5LED4},
        {ui->checkboxPlayer6LED1, ui->checkboxPlayer6LED2, ui->checkboxPlayer6LED3,
         ui->checkboxPlayer6LED4},
        {ui->checkboxPlayer7LED1, ui->checkboxPlayer7LED2, ui->checkboxPlayer7LED3,
         ui->checkboxPlayer7LED4},
        {ui->checkboxPlayer8LED1, ui->checkboxPlayer8LED2, ui->checkboxPlayer8LED3,
         ui->checkboxPlayer8LED4},
    }};

    explain_text_labels = {
        ui->labelPlayer1Explain, ui->labelPlayer2Explain, ui->labelPlayer3Explain,
        ui->labelPlayer4Explain, ui->labelPlayer5Explain, ui->labelPlayer6Explain,
        ui->labelPlayer7Explain, ui->labelPlayer8Explain,
    };

    emulated_controllers = {
        ui->comboPlayer1Emulated, ui->comboPlayer2Emulated, ui->comboPlayer3Emulated,
        ui->comboPlayer4Emulated, ui->comboPlayer5Emulated, ui->comboPlayer6Emulated,
        ui->comboPlayer7Emulated, ui->comboPlayer8Emulated,
    };

    player_labels = {
        ui->labelPlayer1, ui->labelPlayer2, ui->labelPlayer3, ui->labelPlayer4,
        ui->labelPlayer5, ui->labelPlayer6, ui->labelPlayer7, ui->labelPlayer8,
    };

    connected_controller_labels = {
        ui->labelConnectedPlayer1, ui->labelConnectedPlayer2, ui->labelConnectedPlayer3,
        ui->labelConnectedPlayer4, ui->labelConnectedPlayer5, ui->labelConnectedPlayer6,
        ui->labelConnectedPlayer7, ui->labelConnectedPlayer8,
    };

    connected_controller_checkboxes = {
        ui->checkboxPlayer1Connected, ui->checkboxPlayer2Connected, ui->checkboxPlayer3Connected,
        ui->checkboxPlayer4Connected, ui->checkboxPlayer5Connected, ui->checkboxPlayer6Connected,
        ui->checkboxPlayer7Connected, ui->checkboxPlayer8Connected,
    };

    // Setup/load everything prior to setting up connections.
    // This avoids unintentionally changing the states of elements while loading them in.
    SetSupportedControllers();
    DisableUnsupportedPlayers();

    for (std::size_t player_index = 0; player_index < NUM_PLAYERS; ++player_index) {
        SetEmulatedControllers(player_index);
    }

    LoadConfiguration();

    for (std::size_t i = 0; i < NUM_PLAYERS; ++i) {
        SetExplainText(i);
        UpdateControllerIcon(i);
        UpdateLEDPattern(i);
        UpdateBorderColor(i);

        connect(player_groupboxes[i], &QGroupBox::toggled, [this, i](bool checked) {
            if (checked) {
                for (std::size_t index = 0; index <= i; ++index) {
                    connected_controller_checkboxes[index]->setChecked(checked);
                }
            } else {
                for (std::size_t index = i; index < NUM_PLAYERS; ++index) {
                    connected_controller_checkboxes[index]->setChecked(checked);
                }
            }
        });

        connect(emulated_controllers[i], qOverload<int>(&QComboBox::currentIndexChanged),
                [this, i](int) {
                    UpdateControllerIcon(i);
                    UpdateControllerState(i);
                    UpdateLEDPattern(i);
                    CheckIfParametersMet();
                });

        connect(connected_controller_checkboxes[i], &QCheckBox::stateChanged, [this, i](int state) {
            player_groupboxes[i]->setChecked(state == Qt::Checked);
            UpdateControllerIcon(i);
            UpdateControllerState(i);
            UpdateLEDPattern(i);
            UpdateBorderColor(i);
            CheckIfParametersMet();
        });

        if (i == 0) {
            connect(emulated_controllers[i], qOverload<int>(&QComboBox::currentIndexChanged),
                    [this, i](int index) {
                        UpdateDockedState(GetControllerTypeFromIndex(index, i) ==
                                          Core::HID::NpadStyleIndex::Handheld);
                    });
        }
    }

    connect(ui->vibrationButton, &QPushButton::clicked, this,
            &QtControllerSelectorDialog::CallConfigureVibrationDialog);

    connect(ui->motionButton, &QPushButton::clicked, this,
            &QtControllerSelectorDialog::CallConfigureMotionTouchDialog);

    connect(ui->inputConfigButton, &QPushButton::clicked, this,
            &QtControllerSelectorDialog::CallConfigureInputProfileDialog);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this,
            &QtControllerSelectorDialog::ApplyConfiguration);

    // Enhancement: Check if the parameters have already been met before disconnecting controllers.
    // If all the parameters are met AND only allows a single player,
    // stop the constructor here as we do not need to continue.
    if (CheckIfParametersMet() && parameters.enable_single_mode) {
        return;
    }

    // If keep_controllers_connected is false, forcefully disconnect all controllers
    if (!parameters.keep_controllers_connected) {
        for (auto player : player_groupboxes) {
            player->setChecked(false);
        }
    }

    resize(0, 0);
}

QtControllerSelectorDialog::~QtControllerSelectorDialog() {
    system.HIDCore().DisableAllControllerConfiguration();
}

int QtControllerSelectorDialog::exec() {
    if (parameters_met && parameters.enable_single_mode) {
        return QDialog::Accepted;
    }
    return QDialog::exec();
}

void QtControllerSelectorDialog::ApplyConfiguration() {
    const bool pre_docked_mode = Settings::values.use_docked_mode.GetValue();
    Settings::values.use_docked_mode.SetValue(ui->radioDocked->isChecked());
    OnDockedModeChanged(pre_docked_mode, Settings::values.use_docked_mode.GetValue(), system);

    Settings::values.vibration_enabled.SetValue(ui->vibrationGroup->isChecked());
    Settings::values.motion_enabled.SetValue(ui->motionGroup->isChecked());
}

void QtControllerSelectorDialog::LoadConfiguration() {
    system.HIDCore().EnableAllControllerConfiguration();

    const auto* handheld = system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Handheld);
    for (std::size_t index = 0; index < NUM_PLAYERS; ++index) {
        const auto* controller = system.HIDCore().GetEmulatedControllerByIndex(index);
        const auto connected =
            controller->IsConnected(true) || (index == 0 && handheld->IsConnected(true));
        player_groupboxes[index]->setChecked(connected);
        connected_controller_checkboxes[index]->setChecked(connected);
        emulated_controllers[index]->setCurrentIndex(
            GetIndexFromControllerType(controller->GetNpadStyleIndex(true), index));
    }

    UpdateDockedState(handheld->IsConnected(true));

    ui->vibrationGroup->setChecked(Settings::values.vibration_enabled.GetValue());
    ui->motionGroup->setChecked(Settings::values.motion_enabled.GetValue());
}

void QtControllerSelectorDialog::CallConfigureVibrationDialog() {
    ConfigureVibration dialog(this);

    dialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                          Qt::WindowSystemMenuHint);
    dialog.setWindowModality(Qt::WindowModal);

    if (dialog.exec() == QDialog::Accepted) {
        dialog.ApplyConfiguration();
    }
}

void QtControllerSelectorDialog::CallConfigureMotionTouchDialog() {
    ConfigureMotionTouch dialog(this, input_subsystem);

    dialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                          Qt::WindowSystemMenuHint);
    dialog.setWindowModality(Qt::WindowModal);

    if (dialog.exec() == QDialog::Accepted) {
        dialog.ApplyConfiguration();
    }
}

void QtControllerSelectorDialog::CallConfigureInputProfileDialog() {
    ConfigureInputProfileDialog dialog(this, input_subsystem, input_profiles.get(), system);

    dialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                          Qt::WindowSystemMenuHint);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.exec();
}

bool QtControllerSelectorDialog::CheckIfParametersMet() {
    // Here, we check and validate the current configuration against all applicable parameters.
    const auto num_connected_players = static_cast<int>(
        std::count_if(player_groupboxes.begin(), player_groupboxes.end(),
                      [this](const QGroupBox* player) { return player->isChecked(); }));

    const auto min_supported_players = parameters.enable_single_mode ? 1 : parameters.min_players;
    const auto max_supported_players = parameters.enable_single_mode ? 1 : parameters.max_players;

    // First, check against the number of connected players.
    if (num_connected_players < min_supported_players ||
        num_connected_players > max_supported_players) {
        parameters_met = false;
        ui->buttonBox->setEnabled(parameters_met);
        return parameters_met;
    }

    // Next, check against all connected controllers.
    const auto all_controllers_compatible = [this] {
        for (std::size_t index = 0; index < NUM_PLAYERS; ++index) {
            // Skip controllers that are not used, we only care about the currently connected ones.
            if (!player_groupboxes[index]->isChecked() || !player_groupboxes[index]->isEnabled()) {
                continue;
            }

            const auto compatible = IsControllerCompatible(
                GetControllerTypeFromIndex(emulated_controllers[index]->currentIndex(), index),
                parameters);

            // If any controller is found to be incompatible, return false early.
            if (!compatible) {
                return false;
            }
        }

        // Reaching here means all currently connected controllers are compatible.
        return true;
    }();

    parameters_met = all_controllers_compatible;
    ui->buttonBox->setEnabled(parameters_met);
    return parameters_met;
}

void QtControllerSelectorDialog::SetSupportedControllers() {
    const QString theme = [] {
        if (QIcon::themeName().contains(QStringLiteral("dark"))) {
            return QStringLiteral("_dark");
        } else if (QIcon::themeName().contains(QStringLiteral("midnight"))) {
            return QStringLiteral("_midnight");
        } else {
            return QString{};
        }
    }();

    if (parameters.enable_single_mode && parameters.allow_handheld) {
        ui->controllerSupported1->setStyleSheet(
            QStringLiteral("image: url(:/controller/applet_handheld%0); ").arg(theme));
    } else {
        ui->controllerSupported1->setStyleSheet(
            QStringLiteral("image: url(:/controller/applet_handheld%0_disabled); ").arg(theme));
    }

    if (parameters.allow_dual_joycons) {
        ui->controllerSupported2->setStyleSheet(
            QStringLiteral("image: url(:/controller/applet_dual_joycon%0); ").arg(theme));
    } else {
        ui->controllerSupported2->setStyleSheet(
            QStringLiteral("image: url(:/controller/applet_dual_joycon%0_disabled); ").arg(theme));
    }

    if (parameters.allow_left_joycon) {
        ui->controllerSupported3->setStyleSheet(
            QStringLiteral("image: url(:/controller/applet_joycon_left%0); ").arg(theme));
    } else {
        ui->controllerSupported3->setStyleSheet(
            QStringLiteral("image: url(:/controller/applet_joycon_left%0_disabled); ").arg(theme));
    }

    if (parameters.allow_right_joycon) {
        ui->controllerSupported4->setStyleSheet(
            QStringLiteral("image: url(:/controller/applet_joycon_right%0); ").arg(theme));
    } else {
        ui->controllerSupported4->setStyleSheet(
            QStringLiteral("image: url(:/controller/applet_joycon_right%0_disabled); ").arg(theme));
    }

    if (parameters.allow_pro_controller || parameters.allow_gamecube_controller) {
        ui->controllerSupported5->setStyleSheet(
            QStringLiteral("image: url(:/controller/applet_pro_controller%0); ").arg(theme));
    } else {
        ui->controllerSupported5->setStyleSheet(
            QStringLiteral("image: url(:/controller/applet_pro_controller%0_disabled); ")
                .arg(theme));
    }

    // enable_single_mode overrides min_players and max_players.
    if (parameters.enable_single_mode) {
        ui->numberSupportedLabel->setText(QStringLiteral("1"));
        return;
    }

    if (parameters.min_players == parameters.max_players) {
        ui->numberSupportedLabel->setText(QStringLiteral("%1").arg(parameters.max_players));
    } else {
        ui->numberSupportedLabel->setText(
            QStringLiteral("%1 - %2").arg(parameters.min_players).arg(parameters.max_players));
    }
}

void QtControllerSelectorDialog::SetEmulatedControllers(std::size_t player_index) {
    auto& pairs = index_controller_type_pairs[player_index];

    pairs.clear();
    emulated_controllers[player_index]->clear();

    pairs.emplace_back(emulated_controllers[player_index]->count(),
                       Core::HID::NpadStyleIndex::ProController);
    emulated_controllers[player_index]->addItem(tr("Pro Controller"));

    pairs.emplace_back(emulated_controllers[player_index]->count(),
                       Core::HID::NpadStyleIndex::JoyconDual);
    emulated_controllers[player_index]->addItem(tr("Dual Joycons"));

    pairs.emplace_back(emulated_controllers[player_index]->count(),
                       Core::HID::NpadStyleIndex::JoyconLeft);
    emulated_controllers[player_index]->addItem(tr("Left Joycon"));

    pairs.emplace_back(emulated_controllers[player_index]->count(),
                       Core::HID::NpadStyleIndex::JoyconRight);
    emulated_controllers[player_index]->addItem(tr("Right Joycon"));

    if (player_index == 0) {
        pairs.emplace_back(emulated_controllers[player_index]->count(),
                           Core::HID::NpadStyleIndex::Handheld);
        emulated_controllers[player_index]->addItem(tr("Handheld"));
    }

    pairs.emplace_back(emulated_controllers[player_index]->count(),
                       Core::HID::NpadStyleIndex::GameCube);
    emulated_controllers[player_index]->addItem(tr("GameCube Controller"));
}

Core::HID::NpadStyleIndex QtControllerSelectorDialog::GetControllerTypeFromIndex(
    int index, std::size_t player_index) const {
    const auto& pairs = index_controller_type_pairs[player_index];

    const auto it = std::find_if(pairs.begin(), pairs.end(),
                                 [index](const auto& pair) { return pair.first == index; });

    if (it == pairs.end()) {
        return Core::HID::NpadStyleIndex::ProController;
    }

    return it->second;
}

int QtControllerSelectorDialog::GetIndexFromControllerType(Core::HID::NpadStyleIndex type,
                                                           std::size_t player_index) const {
    const auto& pairs = index_controller_type_pairs[player_index];

    const auto it = std::find_if(pairs.begin(), pairs.end(),
                                 [type](const auto& pair) { return pair.second == type; });

    if (it == pairs.end()) {
        return 0;
    }

    return it->first;
}

void QtControllerSelectorDialog::UpdateControllerIcon(std::size_t player_index) {
    if (!player_groupboxes[player_index]->isChecked()) {
        connected_controller_icons[player_index]->setStyleSheet(QString{});
        player_labels[player_index]->show();
        return;
    }

    const QString stylesheet = [this, player_index] {
        switch (GetControllerTypeFromIndex(emulated_controllers[player_index]->currentIndex(),
                                           player_index)) {
        case Core::HID::NpadStyleIndex::ProController:
        case Core::HID::NpadStyleIndex::GameCube:
            return QStringLiteral("image: url(:/controller/applet_pro_controller%0); ");
        case Core::HID::NpadStyleIndex::JoyconDual:
            return QStringLiteral("image: url(:/controller/applet_dual_joycon%0); ");
        case Core::HID::NpadStyleIndex::JoyconLeft:
            return QStringLiteral("image: url(:/controller/applet_joycon_left%0); ");
        case Core::HID::NpadStyleIndex::JoyconRight:
            return QStringLiteral("image: url(:/controller/applet_joycon_right%0); ");
        case Core::HID::NpadStyleIndex::Handheld:
            return QStringLiteral("image: url(:/controller/applet_handheld%0); ");
        default:
            return QString{};
        }
    }();

    if (stylesheet.isEmpty()) {
        connected_controller_icons[player_index]->setStyleSheet(QString{});
        player_labels[player_index]->show();
        return;
    }

    const QString theme = [] {
        if (QIcon::themeName().contains(QStringLiteral("dark"))) {
            return QStringLiteral("_dark");
        } else if (QIcon::themeName().contains(QStringLiteral("midnight"))) {
            return QStringLiteral("_midnight");
        } else {
            return QString{};
        }
    }();

    connected_controller_icons[player_index]->setStyleSheet(stylesheet.arg(theme));
    player_labels[player_index]->hide();
}

void QtControllerSelectorDialog::UpdateControllerState(std::size_t player_index) {
    auto* controller = system.HIDCore().GetEmulatedControllerByIndex(player_index);

    const auto controller_type = GetControllerTypeFromIndex(
        emulated_controllers[player_index]->currentIndex(), player_index);
    const auto player_connected = player_groupboxes[player_index]->isChecked() &&
                                  controller_type != Core::HID::NpadStyleIndex::Handheld;

    if (controller->GetNpadStyleIndex(true) == controller_type &&
        controller->IsConnected(true) == player_connected) {
        return;
    }

    // Disconnect the controller first.
    UpdateController(controller, controller_type, false);

    // Handheld
    if (player_index == 0) {
        if (controller_type == Core::HID::NpadStyleIndex::Handheld) {
            auto* handheld =
                system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Handheld);
            UpdateController(handheld, Core::HID::NpadStyleIndex::Handheld,
                             player_groupboxes[player_index]->isChecked());
        }
    }

    UpdateController(controller, controller_type, player_connected);
}

void QtControllerSelectorDialog::UpdateLEDPattern(std::size_t player_index) {
    if (!player_groupboxes[player_index]->isChecked() ||
        GetControllerTypeFromIndex(emulated_controllers[player_index]->currentIndex(),
                                   player_index) == Core::HID::NpadStyleIndex::Handheld) {
        led_patterns_boxes[player_index][0]->setChecked(false);
        led_patterns_boxes[player_index][1]->setChecked(false);
        led_patterns_boxes[player_index][2]->setChecked(false);
        led_patterns_boxes[player_index][3]->setChecked(false);
        return;
    }

    const auto* controller = system.HIDCore().GetEmulatedControllerByIndex(player_index);
    const auto led_pattern = controller->GetLedPattern();
    led_patterns_boxes[player_index][0]->setChecked(led_pattern.position1);
    led_patterns_boxes[player_index][1]->setChecked(led_pattern.position2);
    led_patterns_boxes[player_index][2]->setChecked(led_pattern.position3);
    led_patterns_boxes[player_index][3]->setChecked(led_pattern.position4);
}

void QtControllerSelectorDialog::UpdateBorderColor(std::size_t player_index) {
    if (!parameters.enable_border_color ||
        player_index >= static_cast<std::size_t>(parameters.max_players) ||
        player_groupboxes[player_index]->styleSheet().contains(QStringLiteral("QGroupBox"))) {
        return;
    }

    player_groupboxes[player_index]->setStyleSheet(
        player_groupboxes[player_index]->styleSheet().append(
            QStringLiteral("QGroupBox#groupPlayer%1Connected:checked "
                           "{ border: 1px solid rgba(%2, %3, %4, %5); }")
                .arg(player_index + 1)
                .arg(parameters.border_colors[player_index][0])
                .arg(parameters.border_colors[player_index][1])
                .arg(parameters.border_colors[player_index][2])
                .arg(parameters.border_colors[player_index][3])));
}

void QtControllerSelectorDialog::SetExplainText(std::size_t player_index) {
    if (!parameters.enable_explain_text ||
        player_index >= static_cast<std::size_t>(parameters.max_players)) {
        return;
    }

    explain_text_labels[player_index]->setText(QString::fromStdString(
        Common::StringFromFixedZeroTerminatedBuffer(parameters.explain_text[player_index].data(),
                                                    parameters.explain_text[player_index].size())));
}

void QtControllerSelectorDialog::UpdateDockedState(bool is_handheld) {
    // Disallow changing the console mode if the controller type is handheld.
    ui->radioDocked->setEnabled(!is_handheld);
    ui->radioUndocked->setEnabled(!is_handheld);

    ui->radioDocked->setChecked(Settings::values.use_docked_mode.GetValue());
    ui->radioUndocked->setChecked(!Settings::values.use_docked_mode.GetValue());

    // Also force into undocked mode if the controller type is handheld.
    if (is_handheld) {
        ui->radioUndocked->setChecked(true);
    }
}

void QtControllerSelectorDialog::DisableUnsupportedPlayers() {
    const auto max_supported_players = parameters.enable_single_mode ? 1 : parameters.max_players;

    switch (max_supported_players) {
    case 0:
    default:
        UNREACHABLE();
        return;
    case 1:
        ui->widgetSpacer->hide();
        ui->widgetSpacer2->hide();
        ui->widgetSpacer3->hide();
        ui->widgetSpacer4->hide();
        break;
    case 2:
        ui->widgetSpacer->hide();
        ui->widgetSpacer2->hide();
        ui->widgetSpacer3->hide();
        break;
    case 3:
        ui->widgetSpacer->hide();
        ui->widgetSpacer2->hide();
        break;
    case 4:
        ui->widgetSpacer->hide();
        break;
    case 5:
    case 6:
    case 7:
    case 8:
        break;
    }

    for (std::size_t index = max_supported_players; index < NUM_PLAYERS; ++index) {
        auto* controller = system.HIDCore().GetEmulatedControllerByIndex(index);
        // Disconnect any unsupported players here and disable or hide them if applicable.
        UpdateController(controller, controller->GetNpadStyleIndex(true), false);
        // Hide the player widgets when max_supported_controllers is less than or equal to 4.
        if (max_supported_players <= 4) {
            player_widgets[index]->hide();
        }

        // Disable and hide the following to prevent these from interaction.
        player_widgets[index]->setDisabled(true);
        connected_controller_checkboxes[index]->setDisabled(true);
        connected_controller_labels[index]->hide();
        connected_controller_checkboxes[index]->hide();
    }
}

QtControllerSelector::QtControllerSelector(GMainWindow& parent) {
    connect(this, &QtControllerSelector::MainWindowReconfigureControllers, &parent,
            &GMainWindow::ControllerSelectorReconfigureControllers, Qt::QueuedConnection);
    connect(&parent, &GMainWindow::ControllerSelectorReconfigureFinished, this,
            &QtControllerSelector::MainWindowReconfigureFinished, Qt::QueuedConnection);
}

QtControllerSelector::~QtControllerSelector() = default;

void QtControllerSelector::ReconfigureControllers(
    std::function<void()> callback_, const Core::Frontend::ControllerParameters& parameters) const {
    callback = std::move(callback_);
    emit MainWindowReconfigureControllers(parameters);
}

void QtControllerSelector::MainWindowReconfigureFinished() {
    callback();
}
