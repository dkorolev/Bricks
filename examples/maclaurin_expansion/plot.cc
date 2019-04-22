/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2019 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

#include "../../blocks/http/api.h"
#include "../../bricks/dflags/dflags.h"
#include "../../bricks/graph/gnuplot.h"

#include "../../optimize/differentiate/differentiate.h"
#include "../../optimize/jit/jit.h"

DEFINE_uint16(port, 3000, "The port to run the server on.");
DEFINE_uint32(derivatives, 11, "The number of derivatives to precompute.");
DEFINE_double(a, -5.0, "The default left end of the plot.");
DEFINE_double(b, +5.0, "The default right end of the plot.");
DEFINE_uint32(n, 1000, "The default number of points.");
DEFINE_bool(log, false, "Set to true for extra terminal output.");

#define USE_FUNCTION(f)                                  \
  inline std::string function_as_string() { return #f; } \
  template <typename X>                                  \
  inline X function(X x) {                               \
    return f;                                            \
  }

USE_FUNCTION(log(1 + exp(x)));

// To make sure the Maclaurin series computation and its derivative are derived correctly, use this:
// USE_FUNCTION(x * (x - 1) * (x + 1) * (x - 2) * (x + 2));

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  using namespace current::expression;
  Vars::Scope vars_scope;

  x["x"] = 0.0;
  std::vector<value_t> f(FLAGS_derivatives + 1u);
  f[0] = function(static_cast<value_t>(x["x"]));
  if (FLAGS_log) {
    std::cerr << "f(x) = " << f[0].DebugAsString() << std::endl;
  }
  for (uint16_t i = 0u; i < FLAGS_derivatives; ++i) {
    f[i + 1u] = Differentiate(f[i], 0u);
    if (FLAGS_log) {
      std::cerr << "f" << std::string(i, '\'') << "(x) = " << f[i + 1u].DebugAsString() << std::endl;
    }
  }

  JITCallContext jit_call_context;
  JITCompiler jit_compiler(jit_call_context);
  std::vector<JITCompiledFunction> ff(FLAGS_derivatives + 1u);
  for (uint16_t i = 0u; i <= FLAGS_derivatives; ++i) {
    ff[i] = jit_compiler.Compile(f[i]);
  }

  auto& http = HTTP(FLAGS_port);
  auto const http_scope = http.Register("/", [&](Request r) {
    using namespace current::gnuplot;

    double const x0 = r.url.query.has("x0") ? current::FromString<double>(r.url.query["x0"]) : 0.0;
    double const a = r.url.query.has("a") ? current::FromString<double>(r.url.query["a"]) : FLAGS_a;
    double const b = r.url.query.has("b") ? current::FromString<double>(r.url.query["b"]) : FLAGS_b;
    uint32_t const n = r.url.query.has("n") ? current::FromString<uint32_t>(r.url.query["n"]) : FLAGS_n;
    uint32_t const d = r.url.query.has("d")
                           ? std::min(FLAGS_derivatives, current::FromString<uint32_t>(r.url.query["d"]))
                           : FLAGS_derivatives;
    bool const derivative = r.url.query.has("derivative");

    std::vector<double> v(d + 1u);
    for (size_t i = 0u; i <= d; ++i) {
      v[i] = ff[i]({x0});
    }

    std::string const svg =
        GNUPlot()
            .Title((derivative ? "Derivative of " : "") + function_as_string() + ", and its Maclaurin approximation")
            .Grid("back")
            .ImageSize(800)
            .OutputFormat("svg")
            .Plot(WithMeta([&](Plotter p) {
                    for (size_t i = 0; i <= n; ++i) {
                      double const x = a + (b - a) * i / n;
                      double v = ff[0]({x});
                      if (derivative) {
                        v = ff[1]({x});
                      }
                      p(x, v);
                    }
                  })
                      .Name("f(x)")
                      .Color("rgb 'orange'")
                      .LineWidth(4))
            .Plot(WithMeta([&](Plotter p) {
                    for (size_t i = 0; i <= n; ++i) {
                      double const x = a + (b - a) * i / n;
                      double const dx = x - x0;
                      double y = 0.0;
                      if (!derivative) {
                        // Compute the Maclaurin approximation of `f(x)`.
                        double k = 1.0;
                        for (size_t j = 0u; j <= d; ++j) {
                          y += v[j] * k;
                          k *= dx / (j + 1u);
                        }
                      } else {
                        // Compute the Maclaurin approximation of `f'(x)`.
                        double k = 1.0;
                        for (size_t j = 1u; j <= d; ++j) {
                          y += v[j] * k;
                          k *= dx / j;
                        }
                      }
                      p(x, y);
                    }
                  })
                      .Name("Maclaurin w/ " + current::ToString(d) + " derivatives")
                      .Color("rgb 'green'")
                      .LineWidth(4))
            .Plot(
                WithMeta([&](Plotter p) { p(x0, v[derivative ? 1u : 0u]); }).AsPoints().Name("x0").Color("rgb 'blue'"));
    r(Response(svg).ContentType(current::net::constants::kDefaultSVGContentType));
  });
  std::cerr << "Server started on http://localhost:" << FLAGS_port << std::endl;

  http.Join();
}
