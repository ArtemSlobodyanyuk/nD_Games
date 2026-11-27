#pragma once

#include "move.h"

namespace nd {
// Розбирає аргументи команди move: звідки куди [N/B/R] [path напрямок номер 1–8].
Move parse_move(const std::string& text);
}
