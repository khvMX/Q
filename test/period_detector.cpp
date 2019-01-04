/*=============================================================================
   Copyright (c) 2014-2018 Joel de Guzman. All rights reserved.

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <infra/doctest.hpp>

#include <q/support/literals.hpp>
#include <q/pitch/period_detector.hpp>
#include <q_io/audio_file.hpp>

#include <vector>
#include <iostream>
#include <tuple>
#include "notes.hpp"

namespace q = cycfi::q;
using namespace q::literals;

constexpr auto pi = q::pi;
constexpr auto sps = 44100;

using result_type
   = std::tuple<float, q::period_detector::info, q::period_detector::info>;

result_type process(
   std::vector<float>&& in
 , q::frequency actual_frequency
 , q::frequency lowest_freq
 , q::frequency highest_freq
 , std::string name)
{
   result_type result;
   constexpr auto n_channels = 2;
   std::vector<float> out(in.size() * n_channels);
   std::get<0>(result) = 0.0f;

   q::period_detector   pd(lowest_freq, highest_freq, sps, -60_dB);
   auto const&          bits = pd.bits();
   auto const&          edges = pd.edges();

   float y = 0.15;
   bool first = true;

   for (auto i = 0; i != in.size(); ++i)
   {
      auto pos = i * n_channels;
      auto ch1 = pos;
      auto ch2 = pos+1;
      auto ch3 = pos+2;

      auto s = in[i];
      out[ch1] = s;

      // Detect period
      pd(s);

      out[ch2] = -1;   // placeholder for bitstream bits

      if (pd.is_ready())
      {
         if (first)
         {
            first = false;
            std::get<1>(result) = pd.first();
            std::get<2>(result) = pd.second();
         }
         else
         {
            CHECK(std::floor(std::get<1>(result)._period)
               == std::floor(pd.first()._period));
         }

         // Print the bitstream bits
         auto frame = edges.frame() + (edges.window_size() / 2);
         auto extra = frame - edges.window_size();
         auto size = bits.size();
         auto out_i = (&out[ch2] - (((size-1) + extra) * n_channels));
         for (auto i = 0; i != size; ++i)
         {
            *out_i = bits.get(i) * 0.8;
            out_i += n_channels;
         }
      }

      if (std::get<0>(result) == 0.0f)
         std::get<0>(result) = pd.predict_period();
   }

   ////////////////////////////////////////////////////////////////////////////
   // Write to a wav file
   q::wav_writer wav{
      "results/period_detect_" + name + ".wav", n_channels, sps
   };
   wav.write(out);

   return result;
}

struct params
{
   float _offset = 0.0;          // Waveform offset
   float _2nd_harmonic = 2;      // Second harmonic multiple
   float _3rd_harmonic = 3;      // Second harmonic multiple
   float _1st_level = 0.3;       // Fundamental level
   float _2nd_level = 0.4;       // Second harmonic level
   float _3rd_level = 0.3;       // Third harmonic level
   float _1st_offset = 0.0;      // Fundamental phase offset
   float _2nd_offset = 0.0;      // Second harmonic phase offset
   float _3rd_offset = 0.0;      // Third harmonic phase offset
};

std::vector<float>
gen_harmonics(q::frequency freq, params const& params_)
{
   auto period = double(sps / freq);
   float offset = params_._offset;
   std::size_t buff_size = sps / (1 / 100E-3); // 100ms

   std::vector<float> signal(buff_size);
   for (int i = 0; i < buff_size; i++)
   {
      auto angle = (i + offset) / period;
      signal[i] += params_._1st_level
         * std::sin(2 * pi * (angle + params_._1st_offset));
      signal[i] += params_._2nd_level
         * std::sin(params_._2nd_harmonic * 2 * pi * (angle + params_._2nd_offset));
      signal[i] += params_._3rd_level
         * std::sin(params_._3rd_harmonic * 2 * pi * (angle + params_._3rd_offset));
   }
   return signal;
}

auto process(
   params const& params_
 , q::frequency actual_frequency
 , q::frequency lowest_freq
 , q::frequency highest_freq
 , char const* name
)
{
   return process(
      gen_harmonics(actual_frequency, params_)
    , actual_frequency, lowest_freq, highest_freq, name
   );
}

using namespace notes;

float max_error = 0.001;   // 0.1% error

void check(float a, float b)
{
   a = std::abs(a);
   b = std::abs(b);
   auto max = std::max(a, b);
   auto diff = std::abs(a - b);
   auto error_percent = max_error * 100;

   CHECK_MESSAGE(diff < (max * max_error),
      "Value exceeded "
      << error_percent
      << "%. Got: "
      << a
      << ",  Expecting: "
      << b
   );
}

void check(q::period_detector::info a, q::period_detector::info b)
{
   check(a._period, b._period);
   check(a._periodicity, b._periodicity);
}

void check_null(q::period_detector::info a)
{
   CHECK(a._period == -1);
   CHECK(a._periodicity == -1);
}

TEST_CASE("100_Hz_pure")
{
   params p;
   p._1st_level = 1.0;
   p._2nd_level = 0.0;
   p._3rd_level = 0.0;
   auto r = process(p, 100_Hz, 100_Hz, 400_Hz, "100_Hz_pure");

   check(std::get<0>(r), 441.0);
   check(std::get<1>(r), { 441.0, 1.0 });
   check_null(std::get<2>(r));
}

TEST_CASE("100_Hz")
{
   auto r = process(params{}, 100_Hz, 100_Hz, 400_Hz, "100_Hz");

   check(std::get<0>(r), 441.0);
   check(std::get<1>(r), { 441.0, 1.0 });
   check_null(std::get<2>(r));
}

TEST_CASE("200_Hz")
{
   auto r = process(params{}, 200_Hz, 100_Hz, 400_Hz, "200_Hz");

   check(std::get<0>(r), 220.5);
   check(std::get<1>(r), { 220.5, 1.0 });
   check_null(std::get<2>(r));
}

TEST_CASE("300_Hz")
{
   auto r = process(params{}, 300_Hz, 100_Hz, 400_Hz, "300_Hz");

   check(std::get<0>(r), 147.0);
   check(std::get<1>(r), { 147.0, 1.0 });
   check_null(std::get<2>(r));
}

TEST_CASE("400_Hz")
{
   auto r = process(params{}, 400_Hz, 100_Hz, 400_Hz, "400_Hz");

   check(std::get<0>(r), 110.25);
   check(std::get<1>(r), { 110.25, 1.0 });
   check_null(std::get<2>(r));
}

TEST_CASE("100_Hz_strong_2nd")
{
   params p;
   p._1st_level = 0.2;
   p._2nd_level = 0.8;
   p._3rd_level = 0.0;
   auto r = process(p, 100_Hz, 100_Hz, 400_Hz, "100_Hz_strong_2nd");

   CHECK(std::get<0>(r) != 0); // expect wrong prediction
   check(std::get<1>(r), { 441.0, 1.0 });
   check(std::get<2>(r), {220.5, 0.915 });
}

TEST_CASE("100_Hz_stronger_2nd")
{
   params p;
   p._1st_level = 0.1;
   p._2nd_level = 0.9;
   p._3rd_level = 0.0;
   auto r = process(p, 100_Hz, 100_Hz, 400_Hz, "100_Hz_stronger_2nd");

   CHECK(std::get<0>(r) != 0); // expect wrong prediction
   check(std::get<1>(r), { 441.0, 1.0 });
   check(std::get<2>(r), {220.5, 0.96 });
}

TEST_CASE("100_Hz_shifted_2nd")
{
   params p;
   p._1st_level = 0.4;
   p._2nd_level = 0.6;
   p._3rd_level = 0.0;
   p._2nd_offset = 0.15;
   auto r = process(p, 100_Hz, 100_Hz, 400_Hz, "100_Hz_shifted_2nd");

   CHECK(std::get<0>(r) != 0); // expect wrong prediction
   check(std::get<1>(r), { 441.0, 1.0 });
   check_null(std::get<2>(r));
}

TEST_CASE("100_Hz_strong_3rd")
{
   params p;
   p._1st_level = 0.4;
   p._2nd_level = 0.0;
   p._3rd_level = 0.6;
   auto r = process(p, 100_Hz, 100_Hz, 400_Hz, "100_Hz_strong_3rd");

   CHECK(std::get<0>(r) != 0); // expect wrong prediction
   check(std::get<1>(r), { 441.0, 1.0 });
   check_null(std::get<2>(r));
}

TEST_CASE("100_Hz_stronger_3rd")
{
   params p;
   p._1st_level = 0.1;
   p._2nd_level = 0.0;
   p._3rd_level = 0.9;
   auto r = process(p, 100_Hz, 100_Hz, 400_Hz, "100_Hz_stronger_3rd");

   CHECK(std::get<0>(r) != 0); // expect wrong prediction
   check(std::get<1>(r), { 441.0, 1.0 });
   check_null(std::get<2>(r));
}

TEST_CASE("100_Hz_missing_fundamental")
{
   params p;
   p._1st_level = 0.0;
   p._2nd_level = 0.6;
   p._3rd_level = 0.4;
   auto r = process(p, 100_Hz, 100_Hz, 400_Hz, "100_Hz_missing_fundamental");

   CHECK(std::get<0>(r) != 0); // expect wrong prediction
   check(std::get<1>(r), { 441.0, 1.0 });
   check(std::get<2>(r), { 220.5, 0.839 });
}

TEST_CASE("Low_E_12th")
{
   auto r = process(params{}, low_e_12th, low_e * 0.8, low_e * 5, "Low_E_12th");

   check(std::get<0>(r), 267.575);
   check(std::get<1>(r), { 267.575, 0.9955 });
   check_null(std::get<2>(r));
}

TEST_CASE("Low_E_24th")
{
   auto r = process(params{}, low_e_24th, low_e * 0.8, low_e * 5, "Low_E_24th");

   check(std::get<0>(r), 133.787);
   check(std::get<1>(r), { 133.787, 0.997 });
   check_null(std::get<2>(r));
}

TEST_CASE("B_24th")
{
   auto r = process(params{}, b_24th, b * 0.8, b * 5, "B_24th");

   check(std::get<0>(r), 44.645);
   check(std::get<1>(r), { 44.645, 0.9955 });
   check_null(std::get<2>(r));
}

TEST_CASE("High_E_24th")
{
   auto r = process(params{}, high_e_24th, high_e * 0.8, high_e * 5, "High_E_24th");

   check(std::get<0>(r), 33.4477);
   check(std::get<1>(r), { 33.4477, 0.984 });
   check_null(std::get<2>(r));
}

TEST_CASE("Non_integer_harmonics")
{
   params p;
   p._offset = 30239;
   p._2nd_harmonic = 2.003;
   auto r = process(p, low_e, low_e * 0.8, low_e * 5, "Non_integer_harmonics");

   CHECK(std::get<0>(r) != 0); // expect wrong prediction
   check(std::get<1>(r), { 534.84, 1.0 });
   check_null(std::get<2>(r));
}








