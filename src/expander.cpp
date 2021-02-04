#include <liquidsfz.hh>
#include <jack/jack.h>
#include <jack/midiport.h>

#include <JackWrap/jackwrap.hpp>

jack_client_t *client = nullptr;
jack_port_t *midi_input_port = nullptr;
jack_port_t *audio_left = nullptr;
jack_port_t *audio_right = nullptr;

LiquidSFZ::Synth synth;

int main(int argc, char const* argv[]) {

  return 0;
}
