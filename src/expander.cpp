#include <5FX/jackwrap.hpp>

#include <sys/types.h>
#include <unistd.h>

#include <liquidsfz.hh>
#include <jack/jack.h>
#include <jack/midiport.h>

#include <lo/lo_cpp.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <fstream>
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <regex>
#include <set>

jack_client_t* client = nullptr;
jack_port_t* midi_input_port = nullptr;
jack_port_t* audio_left = nullptr;
jack_port_t* audio_right = nullptr;

LiquidSFZ::Synth synth;

std::unique_ptr<lo::ServerThread> osc_server;
std::unique_ptr<lo::Address> nsm_server;
std::string nsm_url;
std::atomic_flag nsm_client_opened;
bool has_nsm;

struct Config {
  std::string sound_font;
} config;

struct Session {
  std::string instance_path;
  std::string display_name;
  std::string client_id;
} session;

struct JackClientOpenFailure {};
struct JackPortOpenFailure {};
struct JackCallbackRegisterFailure {};
struct JackClientActivationFailure {};

struct FileOpenFailure { std::string path; };
struct DirectoryCreationFailure { std::string path; };
struct SoundFontLoadingFailure { std::string path; };

struct HomeNotFound {};
struct OSCServerOpenFailure {};

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

std::optional<std::string> get_env(const std::string& var, char const* env[]) {
  std::regex regex(var + "=(.*)");
  std::cmatch match;
  for (int i = 0; env && env[i]; ++i) {
    if (std::regex_match(env[i], match, regex)) {
      return std::make_optional(match[1]);
    }
  }
  return std::nullopt;
}

void open_jack_client(const std::string& name) {

  JackStatus status;
  client = jack_client_open(name.c_str(), JackNullOption, &status);
  if (!client) {
    throw JackClientOpenFailure{};
  }

  midi_input_port = jack_port_register(client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
  audio_left = jack_port_register(client, "out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  audio_right = jack_port_register(client, "out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

  if (!midi_input_port || !audio_left || !audio_right) {
    throw JackPortOpenFailure{};
  }

  if (jack_set_process_callback(client, jack_callback, nullptr)) {
    throw JackCallbackRegisterFailure{};
  }
}

Config default_config(const std::string& home) {
  Config config;
  config.sound_font = home + "/.sfz/Piano/SalamanderGrandPiano/SalamanderGrandPianoV3Retuned.sfz";
  return config;
}
Config load_config_file(const std::string& root) {
  Config config;
  std::string path(root + "/config.cfg");
  std::ifstream file(path);
  if (file.fail()) {
    throw FileOpenFailure{ path };
  }
  file >> config.sound_font;
  file.close();
  return config;
}
void save_config(const Config& config, const std::string& root) {
  if (!std::filesystem::exists(root)) {
    if (!std::filesystem::create_directory(root)) {
      throw DirectoryCreationFailure{ root };
    }
  }
  std::string path(root + "/config.cfg");
  std::ofstream file(path);
  if (file.fail()) {
    throw FileOpenFailure{ path };
  }
  file << config.sound_font;
  file.close();
}

void load_sound_font(const std::string& file) {
  jack_nframes_t samplerate = jack_get_sample_rate(client);
  synth.set_sample_rate(samplerate);
  if (!synth.load(file)) {
    throw SoundFontLoadingFailure{ file };
  }
}

int main(int argc, char const* argv[], char const* env[]) {

  std::srand(std::time(nullptr));

  auto home = get_env("HOME", env);
  if (!home.has_value()) {
    throw HomeNotFound{};
  }

  auto nsm = get_env("NSM_URL", env);
  has_nsm = nsm.has_value();
  if (nsm.has_value()) {

    /* Create connection to NSM server */

    nsm_url = nsm.value();
    nsm_server = std::make_unique<lo::Address>(nsm_url);
    std::cout << "Start under NSM session at : " << nsm_url << std::endl;
    bool success(false);
    for (int i = 0; i < 5; ++i) {
      osc_server = std::make_unique<lo::ServerThread>(8000 + (std::rand() % 1000));
      std::cout << "Port : " << osc_server->port() << std::endl;
      if (osc_server->is_valid()) {
        success = true;
        break;
      }
    }
    if (!success) {
      throw OSCServerOpenFailure{};
    }

    osc_server->add_method("/nsm/client/open", "sss",
      [](lo_arg** argv, int) -> void {
        session.instance_path = &argv[0]->s;
        session.display_name = &argv[1]->s;
        session.client_id = &argv[2]->s;
        nsm_client_opened.clear();
      });
    osc_server->add_method("/nsm/client/save", "",
      [](lo_arg** argv, int) -> void {
        save_config(config, session.instance_path);
        nsm_server->send("/reply", "ss", "/nsm/client/save", "OK");
      });
    osc_server->start();

    synth.set_progress_function(
      [](double progress) -> void {
        nsm_server->send("/nsm/client/progress", "f", progress);
      });

    /* Announce client and wait for response */
    nsm_client_opened.test_and_set();
    nsm_server->send(
      "/nsm/server/announce", "sssiii",
      "5FX-Expander", ":progress:", argv[0], 1, 1, getpid());
    while (nsm_client_opened.test_and_set()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (std::filesystem::exists(session.instance_path)) {
      config = load_config_file(session.instance_path);
    } else {
      config = default_config(home.value());
      save_config(config, session.instance_path);
    }

  } else {
    std::cout << "Start in Standalone mode" << std::endl;

    session.instance_path = home.value() + "/.5FX/5FX-Expander/";
    session.client_id = "5FX-Expander";
    session.display_name = "5FX-Expander";

    config = default_config(home.value());
  }

  /* load session and everything else */
  open_jack_client(session.client_id);
  load_sound_font(config.sound_font);
  if (jack_activate(client)) {
    throw JackClientActivationFailure{};
  }

  if (has_nsm) {
    nsm_server->send("/reply", "ss", "/nsm/client/open", "OK");
  }

  std::cout << "Ready" << std::endl;

  bool run(true);
  do {
    std::string input;
    std::cin >> input;
    run = input != std::string("quit");
  } while (run);

  jack_deactivate(client);
  jack_client_close(client);
  osc_server->stop();

  return 0;
}
