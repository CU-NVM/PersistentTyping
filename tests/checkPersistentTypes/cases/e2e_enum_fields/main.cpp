#include "persistenttype.hpp"
#include <iostream>

enum Color { Red, Green, Blue };
enum class Mode : unsigned char { Off, On };

struct Lamp {
    Color color;
    Mode  mode;
    int   flips;
    Lamp() : color(Red), mode(Mode::Off), flips(0) {}
    void flip() {
        color = color == Red ? Green : Red;
        mode = mode == Mode::Off ? Mode::On : Mode::Off;
        flips++;
    }
};

persistent<Lamp>* lamp;

int main() {
    lamp = new persistent<Lamp>();
    lamp->flip();
    std::cout << "flips " << lamp->flips << " color " << (int)(Color)lamp->color
              << " on " << (lamp->mode == Mode::On) << "\n";
}
