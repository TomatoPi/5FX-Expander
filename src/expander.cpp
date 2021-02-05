#include <5FX/jackwrap.hpp>

#include <sys/types.h>
#include <unistd.h>

#include <liquidsfz.hh>
#include <jack/jack.h>
#include <jack/midiport.h>

#include <lo/lo_cpp.h>

#include <iostream>
#include <optional>
#include <string>
#include <regex>
#include <set>

jack_client_t* client = nullptr;
jack_port_t* midi_input_port = nullptr;
jack_port_t* audio_left = nullptr;
jack_port_t* audio_right = nullptr;

std::set<std::pair<int, int>> sustained_notes;
bool sustain_on;

LiquidSFZ::Synth synth;

lo::ServerThread osc_server(9000);
lo::Address nsm_server("");
std::string nsm_url;
bool has_nsm;

int jack_callback(jack_nframes_t nframes, void* args) {

  void* in_buffer = jack_port_get_buffer(midi_input_port, nframes);
  float* out_buffer[2];
  out_buffer[0] = (float*)jack_port_get_buffer(audio_left, nframes);
  out_buffer[1] = (float*)jack_port_get_buffer(audio_right, nframes);

  jack_nframes_t nevents, i;
  jack_midi_event_t event;

  nevents = jack_midi_get_event_count(in_buffer);
  for (i = 0; i < nevents; ++i) {

    if (jack_midi_event_get(&event, in_buffer, i)) {
      continue;
    }
    if (3 != event.size) {
      continue;
    }

    int channel = event.buffer[0] & 0x0F;

    switch (event.buffer[0] & 0xF0) {
    case 0x90: synth.add_event_note_on(
      event.time, channel, event.buffer[1], event.buffer[2]);
      break;
    case 0x80: synth.add_event_note_off(
      event.time, channel, event.buffer[1]);
      break;
    case 0xb0: synth.add_event_cc(
      event.time, channel, event.buffer[1], event.buffer[2]);
      break;
    case 0xe0: synth.add_event_pitch_bend(
      event.time, channel, event.buffer[1] + event.buffer[2] << 7);
      break;
    }
  }

  synth.process(out_buffer, nframes);

  return 0;
}

std::optional<std::string> get_nsm_url(char const* env[]) {
  std::regex regex("NSM_URL=(.*)");
  std::cmatch match;
  for (int i = 0; env && env[i]; ++i) {
    if (std::regex_match(env[i], match, regex)) {
      return std::make_optional(match[1]);
    }
  }
  return std::nullopt;
}

int main(int argc, char const* argv[], char const* env[]) {

  auto nsm = get_nsm_url(env);
  has_nsm = nsm.has_value();
  if (nsm.has_value()) {
    nsm_url = nsm.value();
    nsm_server = lo::Address(nsm_url);
    std::cout << "Start under NSM session at : " << nsm_url << std::endl;
    if (!osc_server.is_valid()) {
      throw std::string("Failed open OSC Server");
    }
    osc_server.add_method("/nsm/client/open", "sss",
      [](lo_arg** argv, int) -> void {
        nsm_server.send("/reply", "ss", "/nsm/client/open", "OK");
      });
    osc_server.start();
  } else {
    std::cout << "Start in Standalone mode" << std::endl;
  }

  JackStatus status;
  client = jack_client_open("5FX-Expander", JackNullOption, &status);
  if (!client) {
    throw std::string("Failed open Client");
  }

  midi_input_port = jack_port_register(client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
  audio_left = jack_port_register(client, "out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  audio_right = jack_port_register(client, "out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

  if (!midi_input_port || !audio_left || !audio_right) {
    throw std::string("Failed open port");
  }

  if (jack_set_process_callback(client, jack_callback, nullptr)) {
    throw std::string("Failed register process callback");
  }

  jack_nframes_t samplerate = jack_get_sample_rate(client);
  synth.set_sample_rate(samplerate);
  sustain_on = false;

  std::string sfzfile = "/home/tomato/Musique/samples/Pianos/SalamanderGrandPiano/SalamanderGrandPianoV3Retuned.sfz";
  std::cout << "Start loading : " << sfzfile << std::endl;
  if (!synth.load(sfzfile)) {
    throw std::string("Failed load SFZ file : ") + std::string(argv[1]);
  }

  std::cout << "File loaded" << std::endl;
  if (jack_activate(client)) {
    throw std::string("Failed activate client");
  }

  std::cout << "Client Activated\n<< " << std::endl;

  if (has_nsm) {
    nsm_server.send(
      "/nsm/server/announce", "sssiii", 
      "5FX-Expander", ":progress:", argv[0], 1, 1, getpid());
  }

  bool run(true);
  do {
    std::string input;
    std::cin >> input;
    run = input != std::string("quit");
  } while (run);

  jack_deactivate(client);
  jack_client_close(client);
  osc_server.stop();

  return 0;
}
