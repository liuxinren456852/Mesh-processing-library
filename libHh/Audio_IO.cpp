// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "libHh/Audio.h"

#include <mutex>  // once_flag, call_once()

#include "libHh/BinaryIO.h"
#include "libHh/FileIO.h"
#include "libHh/GridOp.h"    // crop()
#include "libHh/StringOp.h"  // get_path_extension()

#define HH_AUDIO_HAVE_FFMPEG  // always as fallback

namespace hh {

namespace {

// The default byte ordering assumed for WAVE data files is little-endian.
// Files written using the big-endian byte ordering scheme have the identifier RIFX instead of RIFF.
// http://soundfile.sapp.org/doc/WaveFormat/
struct WavHeader {
  Vec4<char> ChunkID{'R', 'I', 'F', 'F'};
  uint32_t ChunkSize;
  Vec4<char> Format{'W', 'A', 'V', 'E'};
  Vec4<char> Subcheck1ID{'f', 'm', 't', ' '};
  uint32_t Subchunk1Size{16};  // for PCM
  uint16_t AudioFormat{3};     // 1 == PCM, 3 == float
  uint16_t NumChannels;
  uint32_t SampleRate;
  uint32_t ByteRate;
  uint16_t BlockAlign;
  uint16_t BitsPerSample{sizeof(float) * 8};
  Vec4<char> Subchunk2ID{'d', 'a', 't', 'a'};
  uint32_t Subchunk2Size;
};

static_assert(sizeof(WavHeader) == 44);

}  // namespace

bool ffmpeg_command_exists() {
  static bool s_ret;
  static std::once_flag flag;
  std::call_once(flag, [] {
    s_ret = command_exists_in_path("ffmpeg");
    if (getenv_bool("AUDIO_DEBUG")) SHOW("ffmpeg_command_exists returns", s_ret);
  });
  return s_ret;
}

void Audio::read_file(const string& pfilename) {
  string filename = pfilename;
  const bool ldebug = getenv_bool("AUDIO_DEBUG");
  const bool audio_test_codec = getenv_bool("AUDIO_TEST_CODEC");
  clear();
  unique_ptr<TmpFile> tmpfile;
  if (file_requires_pipe(filename)) {
    RFile fi(filename);
    // Documentation on prefixes for various audio containers:
    //  *.wav: "RIFF^\301\021\000WAVEfmt "
    //  *.mp3: "ID3\004"
    //  *.pcm: binary data in a multitude of formats; ouch.
    int c = fi().peek();
    if (c < 0) throw std::runtime_error("Error reading audio from empty pipe '" + filename + "'");
    attrib().suffix = audio_suffix_for_magic_byte(uchar(c));
    if (attrib().suffix == "")
      throw std::runtime_error(
          sform("Peeked audio format (int(c)=%d) in pipe '%s' not recognized", c, filename.c_str()));
    tmpfile = make_unique<TmpFile>(attrib().suffix, fi());
    filename = tmpfile->filename();
  }
  if (!file_exists(filename)) throw std::runtime_error("Audio file '" + filename + "' does not exist");
  attrib().suffix = to_lower(get_path_extension(filename));
  if (audio_test_codec) {
    if (ldebug) SHOWL;
    assertx(attrib().suffix == "wav");
    WavHeader h;
    RFile fi(filename);
    assertx(read_binary_raw(fi(), ArView(h)));
    from_dos(&h.ChunkSize);
    from_dos(&h.Subchunk1Size);
    from_dos(&h.AudioFormat);
    from_dos(&h.NumChannels);
    from_dos(&h.SampleRate);
    from_dos(&h.ByteRate);
    from_dos(&h.BlockAlign);
    from_dos(&h.BitsPerSample);
    from_dos(&h.Subchunk2Size);
    assertx(h.ChunkID == Vec4<char>{'R', 'I', 'F', 'F'});
    assertx(h.Format == Vec4<char>{'W', 'A', 'V', 'E'});
    assertx(h.Subcheck1ID == Vec4<char>{'f', 'm', 't', ' '});
    assertx(h.Subchunk1Size == 16);
    assertx(h.AudioFormat == 3);
    int t_nchannels = h.NumChannels;
    int t_nsamples = (h.ChunkSize - 36) / t_nchannels / sizeof(float);
    init(V(t_nchannels, t_nsamples));
    assertx(h.ChunkSize == 36 + nsamples() * nchannels() * sizeof(float));
    attrib().samplerate = double(h.SampleRate);
    assertx(h.ByteRate == attrib().samplerate * nchannels() * sizeof(float));
    assertx(h.BlockAlign == nchannels() * sizeof(float));
    assertx(h.BitsPerSample == sizeof(float) * 8);
    assertx(h.Subchunk2ID == Vec4<char>{'d', 'a', 't', 'a'});
    assertx(h.Subchunk2Size == nsamples() * nchannels() * sizeof(float));
    Array<float> ar(nsamples() * nchannels());
    assertx(read_binary_raw(fi(), ar));
    float* p = ar.data();
    for_int(i, nsamples()) for_int(ch, nchannels()) {
      from_dos(&p);
      (*this)(ch, i) = *p++;
    }
  } else {
#if defined(HH_AUDIO_HAVE_FFMPEG)
    if (!ffmpeg_command_exists()) throw std::runtime_error("Cannot find ffmpeg program to read audio content");
    const bool expect_exact_num_samples = attrib().suffix == "wav";
    {  // read header for attributes
      string command = "ffmpeg -nostdin -i " + quote_arg_for_shell(filename) + " -vn -an 2>&1 |";
      if (ldebug) SHOW(command);
      RFile fi(command);
      double duration = -1.;
      int audio_samplerate = -1;
      double audio_bitrate = -1.;
      int audio_nchannels = -1;
      int nlines = 0;
      string line;
      char vch;
      while (my_getline(fi(), line, false)) {
        nlines++;
        if (ldebug) SHOW(line);
        if (contains(line, "Could not find option 'nostdin'")) {
          Warning("Version of external program 'ffmpeg' may be too old");
          continue;
        }
        if (contains(line, "Duration:")) {
          //  Duration: 00:00:05.00, start: 0.000000, bitrate: 32842 kb/s  (some mp4 files)
          //  Duration: 00:00:03.02, start: 0.023021, bitrate: 258 kb/s   (mp3; should be 3.0sec)
          //  Duration: 00:00:05.03, bitrate: 100505 kb/s
          //  Duration: N/A, bitrate: N/A  (invalid.mp4)
          if (contains(line, "Duration: N/A")) throw std::runtime_error("Invalid audio in file '" + filename + "'");
          {
            int vh, vm, vs, vcs;
            assertx(sscanf(line.c_str(), " Duration: %d:%d:%d.%d%c", &vh, &vm, &vs, &vcs, &vch) == 5 && vch == ',');
            duration = vh * 3600. + vm * 60. + vs + vcs * .01;
            if (ldebug) SHOW(vh, vm, vs, vcs, duration);
            string::size_type i = line.find(" start: ");
            if (i != string::npos) {
              double start;
              if (sscanf(line.c_str() + i, " start: %d:%d:%d.%d%c", &vh, &vm, &vs, &vcs, &vch) == 5 && vch == ',') {
                start = vh * 3600. + vm * 60. + vs + vcs * .01;
              } else if (sscanf(line.c_str() + i, " start: %lg%c", &start, &vch) == 2 && vch == ',') {
              } else {
                assertnever(SSHOW(line, line.c_str() + i));
              }
              if (ldebug) SHOW(vh, vm, vs, vcs, duration, start, duration - start);
              duration -= start;
              // It is best not to do explicit "-ss 0 -i" as this starts reading before real start.
            }
          }
        }
        if (contains(line, "Stream #0:") && contains(line, ": Audio:") && contains(line, "kb/s")) {
          // Stream #0:1(eng): Audio: aac (mp4a / 0x6134706D), 48000 Hz, stereo, fltp, 128 kb/s (default)
          // Stream #0:1(und): Audio: aac (mp4a / 0x6134706D), 44100 Hz, mono, fltp, 63 kb/s (default)
          // Stream #0:1(eng): Audio: pcm_s16le (sowt / 0x74776F73), 44100 Hz, mono, s16, 705 kb/s (default)
          // Stream #0:0: Audio: mp3, 48000 Hz, stereo, s16p, 256 kb/s     (mp3 file has no :0 video)
          // Stream #0:0: Audio: pcm_s16le ([1][0][0][0] / 0x0001), 48000 Hz, 2 channels, s16, 1536 kb/s
          // Stream #0:0(eng): Audio: wmav2 (a[1][0][0] / 0x0161), 44100 Hz, 2 channels, fltp, 96 kb/s
          // Stream #0:1(und): Audio: aac (mp4a / 0x6134706D), 48000 Hz, stereo, fltp (default) -- no audio
          string::size_type i;
          i = line.find(" Hz");
          assertx(i != string::npos);
          if (audio_samplerate >= 0.) assertnever("Multiple audio streams inside media container");
          i = line.rfind(", ", i);
          assertx(i != string::npos);
          assertx(sscanf(line.c_str() + i, ", %d H%c", &audio_samplerate, &vch) == 2 && vch == 'z');
          if (contains(line, " mono,"))
            audio_nchannels = 1;
          else if (contains(line, " stereo,"))
            audio_nchannels = 2;
          i = line.find(" channels");
          if (i != string::npos) {
            assertx(audio_nchannels < 0);
            i = line.rfind(", ", i);
            assertx(i != string::npos);
            assertx(sscanf(line.c_str() + i, ", %d channel%c", &audio_nchannels, &vch) == 2 && vch == 's');
          }
          i = line.find(" kb/s");
          assertx(i != string::npos);
          i = line.rfind(", ", i);
          assertx(i != string::npos);
          assertx(sscanf(line.c_str() + i, ", %lg kb/%c", &audio_bitrate, &vch) == 2 && vch == 's');
          audio_bitrate *= 1000.;
        }
      }
      if (ldebug) SHOW(nlines, duration, audio_samplerate, audio_bitrate, audio_nchannels);
      if (!nlines || duration <= 0. || audio_samplerate <= 0 || audio_bitrate <= 0. || audio_nchannels <= 0)
        throw std::runtime_error("ffmpeg is unable to read audio in file '" + filename + "'");
      int nsamples = int(duration * audio_samplerate + .5);
      assertx(nsamples > 0);
      // With (lossy) *.mp3 files, the length changes a bit during encoding.
      const int fudge_reserve_additional_samples = 500;
      if (!expect_exact_num_samples) nsamples += fudge_reserve_additional_samples;
      init(V(audio_nchannels, nsamples));
      attrib().samplerate = audio_samplerate;
      attrib().bitrate = int(audio_bitrate + .5);
    }
    {  // read data
      if (ldebug) SHOW(diagnostic_string());
      // f32be is Big Endian which is standard network order (also s16be == int16_t and u8be == uint8_t)
      string command = ("ffmpeg -v panic -nostdin -i " + quote_arg_for_shell(filename) +
                        " -f f32be -acodec pcm_f32be" + sform(" -af atrim=end_sample=%d", nsamples()) + " - |");
      if (ldebug) SHOW(command);
      RFile fi(command);
      int nread = 0;
      Array<value_type> sample(nchannels());
      for_int(i, nsamples()) {
        if (!read_binary_std(fi(), sample)) break;
        nread++;
        for_int(ch, nchannels()) (*this)(ch, i) = sample[ch];
      }
      if (ldebug) SHOW(nsamples(), nread, nsamples() - nread);
      if (nsamples() != nread) {
        assertx(!expect_exact_num_samples);
        if (nread == 0) {
          // This may be due to older ffmpeg failing to recognize "atrim".
          throw std::runtime_error("ffmpeg is unable to read audio samples in file '" + filename + "'");
        } else {
          *this = crop(*this, twice(0), V(0, nsamples() - nread));  // remove unused samples
        }
      }
      // special_reduce_dim0(nread);
    }
#else  // defined(HH_AUDIO_HAVE_FFMPEG)
    throw std::runtime_error("Audio read is not implemented");
#endif
  }
}

void Audio::write_file(const string& pfilename) const {
  string filename = pfilename;
  const bool ldebug = getenv_bool("AUDIO_DEBUG");
  const bool audio_test_codec = getenv_bool("AUDIO_TEST_CODEC");
  assertx(size());
  assertx(attrib().samplerate);
  if (!attrib().bitrate) {
    Warning("Setting a high audio bitrate");
    const_cast<Audio&>(*this).attrib().bitrate = 256'000;  // mutable
  }
  if (attrib().suffix == "")
    const_cast<Audio&>(*this).attrib().suffix = to_lower(get_path_extension(filename));  // mutable
  if (attrib().suffix == "")
    throw std::runtime_error("Audio '" + filename + "': no filename suffix specified for writing");
  unique_ptr<TmpFile> tmpfile;
  if (file_requires_pipe(filename)) {
    if (filename == "-") my_setenv("NO_DIAGNOSTICS_IN_STDOUT", "1");
    tmpfile = make_unique<TmpFile>(attrib().suffix);
    filename = tmpfile->filename();
  }
  if (audio_test_codec) {
    if (ldebug) SHOWL;
    assertx(attrib().suffix == "wav");
    WavHeader h;
    h.ChunkSize = 36 + nsamples() * nchannels() * sizeof(float);
    h.NumChannels = narrow_cast<uint16_t>(nchannels());
    h.SampleRate = static_cast<uint32_t>(attrib().samplerate + .5);
    h.ByteRate = static_cast<uint32_t>(attrib().samplerate * nchannels() * sizeof(float));
    h.BlockAlign = narrow_cast<uint16_t>(nchannels() * sizeof(float));
    h.Subchunk2Size = nsamples() * nchannels() * sizeof(float);
    to_dos(&h.ChunkSize);
    to_dos(&h.Subchunk1Size);
    to_dos(&h.AudioFormat);
    to_dos(&h.NumChannels);
    to_dos(&h.SampleRate);
    to_dos(&h.ByteRate);
    to_dos(&h.BlockAlign);
    to_dos(&h.BitsPerSample);
    to_dos(&h.Subchunk2Size);
    WFile fi(filename);
    assertx(write_binary_raw(fi(), ArView(h)));
    Array<float> ar;
    for_int(i, nsamples()) for_int(ch, nchannels()) {
      ar.push((*this)(ch, i));
      to_dos(&ar.last());
    }
    assertx(write_binary_raw(fi(), ar));
  } else {
#if defined(HH_AUDIO_HAVE_FFMPEG)
    if (!ffmpeg_command_exists()) throw std::runtime_error("Cannot find ffmpeg program to write audio content");
    {
      string codec;
      if (attrib().suffix == "wav") {
        if (0) codec = " -acodec pcm_s16le";  // ffmpeg default int16_t encoding for *.wav container
        if (1) codec = " -acodec pcm_f32le";  // lossless float representation
      }
      string command = ("| ffmpeg -v panic -f f32be" +
                        sform(" -ar %g -ac %d -i - -ab %d", attrib().samplerate, nchannels(), attrib().bitrate) +
                        codec + " -y " + quote_arg_for_shell(filename));
      if (ldebug) SHOW(command);
      WFile fi(command);
      if (!write_binary_std(fi(), transpose(*this).array_view()))
        throw std::runtime_error("Failed to write audio data");
    }
#else  // defined(HH_AUDIO_HAVE_FFMPEG)
    throw std::runtime_error("Audio write is not implemented");
#endif
  }
  if (tmpfile) tmpfile->write_to(WFile{pfilename}());
}

}  // namespace hh
