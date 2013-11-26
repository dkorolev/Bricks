// The binary to run smoke and perf tests.
//
// TODO(dkorolev): Binary code generation on the fly is not yet implemented.
// 
// Generates random inputs, computes the value of the function using various
// computations techniques (native, intermediate code interpreted, code compiled
// in different languages by different compilers, machine code generation on the fly),
// compares them against each other.

// FNCAS_JIT should be set externally.
// Normally it is done by the running script since it compiles eval.cc with various settings.

#ifndef FNCAS_JIT
#error "FNCAS_JIT should be set to build eval.cc."
#endif

#include <cassert>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "../fncas/fncas.h"

#include "boost/random.hpp"

#include <sys/time.h>

#include "functions.h"

double get_wall_time_seconds() {
  // Single-threaded implementation.
  // #include <chrono> is not friendly with clang++.
  // clock() and boost::time measure CPU time, not wall time.
  // More advanced Boost timer seems to be not present in my Ubuntu 12.04 as of 2013-09-10 - D.K.
  static struct timeval time;
  gettimeofday(&time, NULL);
  return (double)time.tv_sec + (double)time.tv_usec * 1e-6;
}

struct action {
  const F* f;
  uint64_t limit_iterations;
  double limit_seconds;
  std::ostream* sout;
  std::ostream* serr;
  double duration;
  uint64_t iteration = 0;
  bool run(const F* f, double quantity, std::ostream* sout, std::ostream* serr) {
    this->f = f;
    limit_iterations = static_cast<uint64_t>(quantity > 0 ? quantity : 1e12);
    limit_seconds = quantity < 0 ? -quantity : 1e12;
    this->sout = sout;
    this->serr = serr;
    start();
    double begin = get_wall_time_seconds();
    do {
      const bool result = step();
      ++iteration;
      duration = get_wall_time_seconds() - begin;
      if (!result) {
        return false;
      }
    } while (iteration < limit_iterations && duration < limit_seconds);
    done();
    return true;
  }
  virtual void start() {}
  virtual bool step() = 0;
  virtual void done() {
    (*sout) << iteration / duration;
  }
};

struct action_gen : action {
  std::vector<double> x;
  void start() {
    x = std::vector<double>(f->dim());
  }
  bool step() {
    f->gen(x);
    return true;
  }
};

template<typename X> struct action_gen_eval_Xeval : action, X {
  std::vector<double> x;
  std::unique_ptr<fncas::f> fncas_f;
  double compile_time;
  void start() {
    fncas_f = X::init(f);
    x = std::vector<double>(f->dim());
  }
  bool step() {
    f->gen(x);
    const double golden = f->eval_as_double(x);
    const double test = fncas_f->invoke(x);
    if (test == golden) {
      return true;
    } else {
      (*serr) << golden << " != " << test << " @" << iteration;
      return false;
    }
  }
  virtual void done() {
    action::done();
    X::done(*sout);
  }
};

// Evaluators to compare against result- and performance-wise.
struct eval {
  // Baseline code.
  struct base {
    void done(std::ostream& os) {
    }
  };
  // Native implementation calls the function natively compiled as part of the binary being run.
  struct native : base {
    struct impl : fncas::f {
      const F* f_;
      impl(const F* f) : f_(f) {
      }
      virtual double invoke(const std::vector<double>& x) const {
        return f_->eval_as_double(x);
      }
    };
    std::unique_ptr<fncas::f> init(const F* f) {
      return std::unique_ptr<fncas::f>(new impl(f));
    }
  };
  // Intermeridate implementation calls fncas implemenation
  // that interprets the internal representation of the function.
  struct intermediate : base {
    struct impl : fncas::f {
      const fncas::output<fncas::x>::type e_;
      impl(const F* f) : e_(f->eval_as_expression(fncas::x(f->dim()))) {
      }
      virtual double invoke(const std::vector<double>& x) const {
        return e_.eval(x);
      }
    };
    std::unique_ptr<fncas::f> init(const F* f) {
      return std::unique_ptr<fncas::f>(new impl(f));  //->eval_as_expression(fncas::x(f->dim()))));
    }
  };
  // Compiled implementation calls fncas implementation
  // that invokes an externally compiled version of the function.
  // The compilation takes place upon the construction of this object.
  struct compiled : base {
    struct impl : fncas::f {
      std::unique_ptr<fncas::compiled_expression> c_;
      double compile(const F* f) {
        const double begin = get_wall_time_seconds();
        c_ = fncas::compile(f->eval_as_expression(fncas::x(f->dim())));
        const double end = get_wall_time_seconds();
        return end - begin;
      }
      virtual double invoke(const std::vector<double>& x) const {
        return c_->eval(x);
      }
    };
    double compile_time;
    std::unique_ptr<fncas::f> init(const F* f) {
      std::unique_ptr<impl> p(new impl());
      compile_time = p->compile(f);
      return std::unique_ptr<fncas::f>(p.release());
    }
    void done(std::ostream& os) {
      os << ':' << compile_time;
    }
  };
};

typedef action_gen_eval_Xeval<eval::native> action_gen_eval_eval;
typedef action_gen_eval_Xeval<eval::intermediate> action_gen_eval_ieval;
typedef action_gen_eval_Xeval<eval::compiled> action_gen_eval_ceval;

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <function> <action> <iterations or -seconds>" << std::endl;
    return -1;
  } else {
    const char* function_name = argv[1];
    const char* action_name = argv[2];
    const double quantity = (argc >= 4) ? atof(argv[3]) : 1000;  // 1000 iterations by default.
    const F* f = registered_functions[function_name];
    if (!f) {
      std::cerr << "Function '" << function_name << "' is not defined in functions/*.h." << std::endl;
      return -1;
    } else {
      typedef boost::function<int(const F*, double, std::ostream&, std::ostream&)> F_ACTION;
      std::map<std::string, std::unique_ptr<action>> actions;
      actions["gen"].reset(new action_gen());
      actions["gen_eval_eval"].reset(new action_gen_eval_eval());
      actions["gen_eval_ieval"].reset(new action_gen_eval_ieval());
      actions["gen_eval_ceval"].reset(new action_gen_eval_ceval());
      action* action_handler = actions[action_name].get();
      if (!action_handler) {
        std::cerr << "Action '" << action_name << "' is not defined." << std::endl;
        return -1;
      } else {
        std::ostringstream sout;
        std::ostringstream serr;
        sout << std::fixed << std::setprecision(5);
        serr << std::fixed << std::setprecision(5);
        if (action_handler->run(f, quantity, &sout, &serr)) {
          if (quantity < 0) {
            // Only output perf stats in perf test mode,
            // leave the output blank in smoke test mode in case of no errors.
            std::cout << sout.str() << std::endl;
          }
          return 0;
        } else {
          std::cout << serr.str() << std::endl;
          std::cerr << serr.str() << std::endl;
          return 1;
        }
      }
    }
  }
}
