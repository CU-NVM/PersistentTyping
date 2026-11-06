#include "persistenttype.hpp"

void fill(char* buf) { buf[0] = 'x'; }

struct Note {
    char* text;
    Note() { text = new char[8]; fill(text); }
};

persistent<Note>* note;

int main() { note = new persistent<Note>(); }
