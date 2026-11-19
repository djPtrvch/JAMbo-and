// Мастер: константы, нужные справочникам (порт части enum MasterChain, Core/Master/MasterChain.swift, ветка swift
// JAMbo); остальное — при переносе мастера.
#pragma once

namespace jambo {
namespace MasterChain {

/// RELEASE компрессора и лимитера ≥ autoValue — AUTO.
constexpr double autoValue = 1e9;
/// ATTACK компрессора нового проекта — 30 мс.
constexpr double newAttack = 30;

}  // namespace MasterChain
}  // namespace jambo
