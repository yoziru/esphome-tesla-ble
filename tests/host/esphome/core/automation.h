#pragma once
namespace esphome {
template<typename... Ts> struct Action { virtual void play(Ts...) = 0; };
template<typename T, typename... Ts> struct TemplatableValue { T value(Ts...) { return T{}; } };
}
