#include <iostream>

#include "base/config/config.h"
#include "base/util/runfiles_db.h"
#include "tile/codegen/driver.h"
#include "tile/lang/gen_stripe.h"
#include "tile/lib/lib.h"
#include "tile/stripe/stripe.h"
#include "tile/targets/cpu/jit.h"

static std::string ReadFile(const std::string& filename) {
  std::ifstream ifs;
  ifs.open(filename);
  if (ifs.fail()) {
    throw(std::runtime_error("Unable to open file \"" + filename + "\""));
  }
  auto it = std::istreambuf_iterator<char>(ifs);
  auto it_end = std::istreambuf_iterator<char>();
  std::string contents(it, it_end);
  if (ifs.bad()) {
    throw(std::runtime_error("Unable to fully read \"" + filename + "\""));
  }
  return contents;
}

template <typename F>
void with_profile(F f) {
  auto start = std::chrono::high_resolution_clock::now();
  f();
  auto d = std::chrono::high_resolution_clock::now() - start;
  std::cout << "Execution took: " << std::chrono::duration<double>(d).count() * 1000 << "ms" << std::endl;
}

int main(int argc, char* argv[]) {
  START_EASYLOGGINGPP(argc, argv);
  el::Loggers::setVerboseLevel(2);

  using namespace vertexai::tile;  // NOLINT
  std::cout << "Hey!" << std::endl;

  // Express
  auto in1 = SimpleShape(DataType::FLOAT32, {1024, 1024});
  auto in2 = SimpleShape(DataType::FLOAT32, {1024, 1024});
  auto runinfo = lib::LoadMatMul("test", in1, in2);
  auto block = lang::GenerateStripe(runinfo).program;
  std::cout << *block << std::endl;

  // static vertexai::RunfilesDB runfiles_db{"com_intel_plaidml"};
  //    std::string cfg_file = runfiles_db["tile/cpu/cpu.json"];
  std::string cfg_file = "external/com_intel_plaidml/tile/cpu/cpu.json";

  auto cfg = vertexai::ParseConfig<codegen::proto::Config>(ReadFile(cfg_file));
  codegen::OptimizeOptions options = {
      true,                      // dump_passes
      false,                     // dump_code
      "/tmp/stripe_cpu/passes",  // dbg_dir
  };
  codegen::Optimize(block.get(), cfg.passes(), options);

  std::cout << "============================================================\n" << *block << std::endl;

  // Run
  std::vector<float> a_data(1024 * 1024);
  std::vector<float> b_data(1024 * 1024);
  std::vector<float> c_data(1024 * 1024);

  a_data[0] = 1.f;
  b_data[0] = 1.f;

  std::map<std::string, void*> io;
  io["A"] = a_data.data();
  io["B"] = b_data.data();
  io["C"] = c_data.data();

  targets::cpu::Native native;
  native.compile(*block);

  for (int i = 0; i < 10; i++) {
    for (auto& f : c_data) {
      f = 0.f;
    }
    with_profile([&]() {  //
      native.run(io);
    });
  }

  std::cout << c_data[0] << std::endl;

  return 0;
}