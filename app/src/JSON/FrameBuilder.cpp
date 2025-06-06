/*
 * Serial Studio - https://serial-studio.github.io/
 *
 * Copyright (C) 2020-2025 Alex Spataru <https://aspatru.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QFileInfo>
#include <QFileDialog>

#include "IO/Manager.h"
#include "Misc/Utilities.h"

#include "CSV/Player.h"
#include "JSON/ProjectModel.h"
#include "JSON/FrameBuilder.h"

#ifdef USE_QT_COMMERCIAL
#  include "Licensing/LemonSqueezy.h"
#endif

/**
 * Initializes the JSON Parser class and connects appropiate SIGNALS/SLOTS
 */
JSON::FrameBuilder::FrameBuilder()
  : m_opMode(SerialStudio::ProjectFile)
  , m_frameParser(nullptr)
{
  // Read JSON map location
  auto path = m_settings.value("json_map_location", "").toString();
  if (!path.isEmpty())
    loadJsonMap(path);

  // Obtain operation mode from settings
  auto m = m_settings.value("operation_mode", SerialStudio::QuickPlot).toInt();
  setOperationMode(static_cast<SerialStudio::OperationMode>(m));

  // Reload JSON map file when license is activated
#ifdef USE_QT_COMMERCIAL
  connect(&Licensing::LemonSqueezy::instance(),
          &Licensing::LemonSqueezy::activatedChanged, this, [=] {
            if (!jsonMapFilepath().isEmpty())
              loadJsonMap(jsonMapFilepath());
          });
#endif
}

/**
 * Returns the only instance of the class
 */
JSON::FrameBuilder &JSON::FrameBuilder::instance()
{
  static FrameBuilder singleton;
  return singleton;
}

/**
 * Returns the file path of the loaded JSON map file
 */
QString JSON::FrameBuilder::jsonMapFilepath() const
{
  if (m_jsonMap.isOpen())
  {
    auto fileInfo = QFileInfo(m_jsonMap.fileName());
    return fileInfo.filePath();
  }

  return "";
}

/**
 * Returns the file name of the loaded JSON map file
 */
QString JSON::FrameBuilder::jsonMapFilename() const
{
  if (m_jsonMap.isOpen())
  {
    auto fileInfo = QFileInfo(m_jsonMap.fileName());
    return fileInfo.fileName();
  }

  return "";
}

/**
 * Returns a pointer to the currently loaded frame parser editor.
 */
JSON::FrameParser *JSON::FrameBuilder::frameParser() const
{
  return m_frameParser;
}

/**
 * Returns the operation mode
 */
SerialStudio::OperationMode JSON::FrameBuilder::operationMode() const
{
  return m_opMode;
}

/**
 * Creates a file dialog & lets the user select the JSON file map
 */
void JSON::FrameBuilder::loadJsonMap()
{
  const auto file = QFileDialog::getOpenFileName(
      nullptr, tr("Select JSON map file"),
      JSON::ProjectModel::instance().jsonProjectsPath(),
      tr("JSON files") + QStringLiteral(" (*.json)"));

  if (!file.isEmpty())
    loadJsonMap(file);
}

/**
 * Configures the signal/slot connections with the rest of the modules of the
 * application.
 */
void JSON::FrameBuilder::setupExternalConnections()
{
  connect(&IO::Manager::instance(), &IO::Manager::frameReceived, this,
          &JSON::FrameBuilder::readData, Qt::QueuedConnection);
}

/**
 * Opens, validates & loads into memory the JSON file in the given @a path.
 */
void JSON::FrameBuilder::loadJsonMap(const QString &path)
{
  // Validate path
  if (path.isEmpty())
    return;

  // Close previous file (if open)
  if (m_jsonMap.isOpen())
  {
    m_frame.clear();
    m_jsonMap.close();
    Q_EMIT jsonFileMapChanged();
  }

  // Try to open the file (read only mode)
  m_jsonMap.setFileName(path);
  if (m_jsonMap.open(QFile::ReadOnly))
  {
    // Read data & validate JSON from file
    QJsonParseError error;
    auto data = m_jsonMap.readAll();
    auto document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError)
    {
      m_frame.clear();
      m_jsonMap.close();
      setJsonPathSetting("");
      Misc::Utilities::showMessageBox(
          tr("JSON parse error"), error.errorString(), QMessageBox::Critical);
    }

    // JSON contains no errors, load compacted JSON document & save settings
    else
    {
      // Save settings
      setJsonPathSetting(path);

      // Load frame from data
      m_frame.clear();
      const bool ok = m_frame.read(document.object());

      // Update I/O manager settings
      if (ok && m_frame.isValid())
      {
        if (operationMode() == SerialStudio::ProjectFile)
        {
          IO::Manager::instance().setFinishSequence(m_frame.frameEnd());
          IO::Manager::instance().setStartSequence(m_frame.frameStart());
        }
      }

      // Invalid frame data
      else
      {
        m_frame.clear();
        m_jsonMap.close();
        setJsonPathSetting("");
        Misc::Utilities::showMessageBox(tr("Invalid JSON project format"));
      }
    }

    // Get rid of warnings
    Q_UNUSED(document);
  }

  // Open error
  else
  {
    setJsonPathSetting("");
    Misc::Utilities::showMessageBox(
        tr("Cannot read JSON file"),
        tr("Please check file permissions & location"), QMessageBox::Critical);
    m_jsonMap.close();
  }

  // Update UI
  Q_EMIT jsonFileMapChanged();
}

/**
 * @brief Assigns an instance to the frame parser to be used to split frame
 *        data/elements into individual parts.
 */
void JSON::FrameBuilder::setFrameParser(JSON::FrameParser *parser)
{
  m_frameParser = parser;
}

/**
 * Changes the operation mode of the JSON parser. There are two possible op.
 * modes:
 *
 * @c kManual serial data only contains the comma-separated values, and we need
 *            to use a JSON map file (given by the user) to know what each value
 *            means. This method is recommended when we need to transfer &
 *            display a large amount of information from the microcontroller
 *            unit to the computer.
 *
 * @c kAutomatic serial data contains the JSON data frame, good for simple
 *               applications or for prototyping.
 */
void JSON::FrameBuilder::setOperationMode(
    const SerialStudio::OperationMode mode)
{
  m_opMode = mode;

  switch (mode)
  {
    case SerialStudio::DeviceSendsJSON:
      IO::Manager::instance().setStartSequence("");
      IO::Manager::instance().setFinishSequence("");
      break;
    case SerialStudio::ProjectFile:
      IO::Manager::instance().setFinishSequence(m_frame.frameEnd());
      IO::Manager::instance().setStartSequence(m_frame.frameStart());
      break;
    case SerialStudio::QuickPlot:
      IO::Manager::instance().setStartSequence("");
      IO::Manager::instance().setFinishSequence("");
      break;
    default:
      qWarning() << "Invalid operation mode selected" << mode;
      break;
  }

  m_settings.setValue("operation_mode", mode);
  Q_EMIT operationModeChanged();
}

/**
 * Saves the location of the last valid JSON map file that was opened (if any)
 */
void JSON::FrameBuilder::setJsonPathSetting(const QString &path)
{
  m_settings.setValue(QStringLiteral("json_map_location"), path);
}

/**
 * Tries to parse the given data as a JSON document according to the selected
 * operation mode.
 *
 * Possible operation modes:
 * - Auto:   serial data contains the JSON data frame
 * - Manual: serial data only contains the comma-separated values, and we need
 *           to use a JSON map file (given by the user) to know what each value
 *           means
 *
 * If JSON parsing is successfull, then the class shall notify the rest of the
 * application in order to process packet data.
 */
void JSON::FrameBuilder::readData(const QByteArray &data)
{
  // Data empty, abort
  if (data.isEmpty())
    return;

  // Serial device sends JSON (auto mode)
  if (operationMode() == SerialStudio::DeviceSendsJSON)
  {
    auto jsonData = QJsonDocument::fromJson(data).object();
    if (m_frame.read(jsonData))
      Q_EMIT frameChanged(m_frame);
  }

  // Data is separated and parsed by Serial Studio project
  else if (operationMode() == SerialStudio::ProjectFile && m_frameParser)
  {
    // Obtain state of the app
    const bool csvPlaying = CSV::Player::instance().isOpen();

    // Real-time data, parse data & perform conversion
    QStringList fields;
    if (!csvPlaying)
    {
      // Convert binary frame data to a string
      QString frameData;
      switch (JSON::ProjectModel::instance().decoderMethod())
      {
        case SerialStudio::PlainText:
          frameData = QString::fromUtf8(data);
          break;
        case SerialStudio::Hexadecimal:
          frameData = QString::fromUtf8(data.toHex());
          break;
        case SerialStudio::Base64:
          frameData = QString::fromUtf8(data.toBase64());
          break;
        default:
          frameData = QString::fromUtf8(data);
          break;
      }

      // Get fields from frame parser function
      fields = m_frameParser->parse(frameData);
    }

    // CSV data, no need to perform conversions or use frame parser
    else
      fields = QString::fromUtf8(data.simplified()).split(',');

    // Replace data in frame
    for (int g = 0; g < m_frame.groupCount(); ++g)
    {
      auto &group = m_frame.m_groups[g];
      for (int d = 0; d < group.datasetCount(); ++d)
      {
        auto &dataset = group.m_datasets[d];
        const auto index = dataset.index();
        if (index <= fields.count() && index > 0)
          dataset.m_value = fields.at(index - 1);
      }
    }

    // Update user interface
    Q_EMIT frameChanged(m_frame);
  }

  // Data is separated by comma separated values
  else if (operationMode() == SerialStudio::QuickPlot)
  {
    // Obtain fields from data frame
    auto fields = data.split(',');

    // Create datasets from the data
    int channel = 1;
    QVector<JSON::Dataset> datasets;
    for (const auto &field : std::as_const(fields))
    {
      JSON::Dataset dataset;
      dataset.m_groupId = 0;
      dataset.m_index = channel;
      dataset.m_title = tr("Channel %1").arg(channel);
      dataset.m_value = QString::fromUtf8(field);
      dataset.m_graph = false;
      datasets.append(dataset);

      ++channel;
    }

    // Create a project frame from the groups
    JSON::Frame frame;
    frame.m_title = tr("Quick Plot");

    // Create a datagrid group from the dataset array
    JSON::Group datagrid(0);
    datagrid.m_datasets = datasets;
    datagrid.m_title = tr("Quick Plot Data");
    datagrid.m_widget = QStringLiteral("datagrid");

    // Append datagrid to frame
    frame.m_groups.append(datagrid);

    // Create a multiplot group when multiple datasets are found
    if (datasets.count() > 1)
    {
      JSON::Group multiplot(1);
      multiplot.m_datasets = datasets;
      multiplot.m_title = tr("Multiple Plots");
      multiplot.m_widget = QStringLiteral("multiplot");
      for (int i = 0; i < multiplot.m_datasets.count(); ++i)
        multiplot.m_datasets[i].m_groupId = 1;

      frame.m_groups.append(multiplot);
    }

    // Create a container group with plots
    JSON::Group plots(2);
    plots.m_datasets = datasets;
    plots.m_widget = QLatin1String("");
    plots.m_title = tr("Individual Plots");
    for (int i = 0; i < plots.m_datasets.count(); ++i)
    {
      plots.m_datasets[i].m_groupId = 2;
      plots.m_datasets[i].m_graph = true;
      plots.m_datasets[i].m_displayInOverview = (plots.m_datasets.count() == 1);
    }

    // Register container group
    frame.m_groups.append(plots);

    Q_EMIT frameChanged(frame);
  }
}
