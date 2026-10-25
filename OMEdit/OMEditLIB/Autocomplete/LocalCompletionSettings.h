/*
 * This file is part of OpenModelica.
 *
 * Copyright (c) 1998-2026, Open Source Modelica Consortium (OSMC),
 * c/o Linköpings universitet, Department of Computer and Information Science,
 * SE-58183 Linköping, Sweden.
 *
 * All rights reserved.
 *
 * THIS PROGRAM IS PROVIDED UNDER THE TERMS OF AGPL VERSION 3 LICENSE OR
 * THIS OSMC PUBLIC LICENSE (OSMC-PL) VERSION 1.8.
 * ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS PROGRAM CONSTITUTES
 * RECIPIENT'S ACCEPTANCE OF THE OSMC PUBLIC LICENSE OR THE GNU AGPL
 * VERSION 3, ACCORDING TO RECIPIENTS CHOICE.
 *
 * The OpenModelica software and the OSMC (Open Source Modelica Consortium)
 * Public License (OSMC-PL) are obtained from OSMC, either from the above
 * address, from the URLs:
 * http://www.openmodelica.org or
 * https://github.com/OpenModelica/ or
 * http://www.ida.liu.se/projects/OpenModelica,
 * and in the OpenModelica distribution.
 *
 * GNU AGPL version 3 is obtained from:
 * https://www.gnu.org/licenses/licenses.html#GPL
 *
 * This program is distributed WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE, EXCEPT AS EXPRESSLY SET FORTH
 * IN THE BY RECIPIENT SELECTED SUBSIDIARY LICENSE CONDITIONS OF OSMC-PL.
 *
 * See the full OSMC Public License conditions for more details.
 *
 */

#ifndef LOCALCOMPLETIONSETTINGS_H
#define LOCALCOMPLETIONSETTINGS_H

#include <QString>
#include <cmath>

struct LocalCompletionSettings
{
  bool enabled = false;
  bool automatic = true;
  QString endpoint = QStringLiteral("http://127.0.0.1:11434/v1");
  QString model = QStringLiteral("qwen2.5-coder:1.5b-base");
  QString shortcut = QStringLiteral("Ctrl+Alt+Space");
  int debounceMs = 150;
  int timeoutMs = 5000;
  int maxLines = 6;
  int maxTokens = 128;
  int prefixCharacters = 12000;
  int suffixCharacters = 4000;
  double temperature = 0.0;
  QString instructions;
  int ghostOpacity = 47;
  bool ghostItalic = false;
  QString acceptShortcut = QStringLiteral("Tab");
  bool triggerAfterDeletion = true;
  int minimumPrefixLength = 0;

  LocalCompletionSettings normalized() const
  {
    LocalCompletionSettings result = *this;
    result.debounceMs = qBound(50, debounceMs, 2000);
    result.timeoutMs = qBound(1, timeoutMs, 60000);
    result.maxLines = qBound(1, maxLines, 50);
    result.maxTokens = qBound(1, maxTokens, 2048);
    result.prefixCharacters = qBound(0, prefixCharacters, 64000);
    result.suffixCharacters = qBound(0, suffixCharacters, 16000);
    result.temperature = std::isfinite(temperature) ? qBound(0.0, temperature, 2.0) : 0.0;
    result.instructions = instructions.left(4000);
    result.ghostOpacity = qBound(10, ghostOpacity, 90);
    result.minimumPrefixLength = qBound(0, minimumPrefixLength, 16);
    if (acceptShortcut != QStringLiteral("Tab") && acceptShortcut != QStringLiteral("Ctrl+Right")
        && acceptShortcut != QStringLiteral("Ctrl+Return")) {
      result.acceptShortcut = QStringLiteral("Tab");
    }
    return result;
  }

  bool operator==(const LocalCompletionSettings &other) const
  {
    return enabled == other.enabled && automatic == other.automatic && endpoint == other.endpoint
        && model == other.model && shortcut == other.shortcut && debounceMs == other.debounceMs
        && timeoutMs == other.timeoutMs && maxLines == other.maxLines && maxTokens == other.maxTokens
        && prefixCharacters == other.prefixCharacters && suffixCharacters == other.suffixCharacters
        && temperature == other.temperature && instructions == other.instructions
        && ghostOpacity == other.ghostOpacity && ghostItalic == other.ghostItalic
        && acceptShortcut == other.acceptShortcut && triggerAfterDeletion == other.triggerAfterDeletion
        && minimumPrefixLength == other.minimumPrefixLength;
  }
};

#endif
