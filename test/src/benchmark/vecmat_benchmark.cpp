#include <benchmark/benchmark.h>

#include <random>
#include <math/vecmat.h> //fso linear algebra module

#include <../lib/eigen/Eigen/Core>
#include <../lib/eigen/Eigen/Geometry>


static void b_vec_add(benchmark::State& state) {

	std::random_device rd;
	std::mt19937 gen(rd());

	std::uniform_real_distribution<float> d(0, 10000);

	vec3d vec1 = {d(gen), d(gen), d(gen)};
    vec3d vec2 = {d(gen), d(gen), d(gen)};

	while (state.KeepRunning()) {
        vec3d result;

        vm_vec_add(&result, &vec1, &vec2);

		benchmark::DoNotOptimize(result);
	}
}

static void b_vec_add_eigen(benchmark::State& state) {

    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_real_distribution<float> d(0, 10000);

    Eigen::Vector3f vec1(d(gen), d(gen), d(gen));
    Eigen::Vector3f vec2(d(gen), d(gen), d(gen));

    while (state.KeepRunning()) {
        Eigen::Vector3f result = vec1 + vec2;

        benchmark::DoNotOptimize(result);
    }
}

static void b_mat_vec_transform_eigen(benchmark::State& state) {

    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_real_distribution<float> d(0, 10000);

    Eigen::Vector3f vec(d(gen), d(gen), d(gen));
    Eigen::Affine3f mat = Eigen::Translation3f(Eigen::Vector3f::Random()) * Eigen::AngleAxisf(d(gen),Eigen::Vector3f::Random());

    while (state.KeepRunning()) {
        Eigen::Vector3f result = mat * vec;

        benchmark::DoNotOptimize(result);
    }
}

static void b_mat_vec_transform(benchmark::State& state) {

    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_real_distribution<float> d(0, 10000);

    vec3d vec1 = {d(gen), d(gen), d(gen)};
    vec3d translate = {d(gen), d(gen), d(gen)};

    matrix rotate = {d(gen), d(gen), d(gen), d(gen), d(gen), d(gen), d(gen), d(gen), d(gen)};

    while (state.KeepRunning()) {
        vec3d tmp;
        vm_vec_unrotate(&tmp, &vec1, &rotate);
        vec3d result;
        vm_vec_add(&result, &tmp, &translate);

        benchmark::DoNotOptimize(result);
    }
}

static void b_mat_vec_transform_array(benchmark::State& state) {

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> d(0, 10000);

    std::vector<vec3d> vec1(state.range(0));
    std::vector<vec3d> result(state.range(0));
    std::generate(vec1.begin(), vec1.end(), [&]() {return vec3d{d(gen), d(gen), d(gen)};});
    std::generate(result.begin(), result.end(), [&]() {return vec3d{d(gen), d(gen), d(gen)};});

    vec3d translate = {d(gen), d(gen), d(gen)};
    matrix rotate = {d(gen), d(gen), d(gen), d(gen), d(gen), d(gen), d(gen), d(gen), d(gen)};

    while (state.KeepRunning()) {

        for ( size_t i = 0; i != vec1.size(); ++i) {
            vec3d tmp;
            vm_vec_unrotate(&tmp, &vec1[i], &rotate);
            vm_vec_add(&result[i], &tmp, &translate);
        }

        benchmark::DoNotOptimize(result);
    }
}

static void b_mat_vec_transform_array_eigen(benchmark::State& state) {

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> d(0, 10000);

    std::vector<Eigen::Vector3f> vec(state.range(0));
    std::vector<Eigen::Vector3f> result(state.range(0));
    std::generate(vec.begin(), vec.end(), [&]() {return Eigen::Vector3f{d(gen), d(gen), d(gen)};});

    Eigen::Affine3f mat = Eigen::Translation3f(Eigen::Vector3f::Random()) * Eigen::AngleAxisf(d(gen),Eigen::Vector3f::Random());

    while (state.KeepRunning()) {

        for ( size_t i = 0; i != vec.size(); ++i) {
            result[i] = mat * vec[i];
        }

        benchmark::DoNotOptimize(result);
    }
}

constexpr auto benchmark_size = 2 << 15;

BENCHMARK(b_vec_add);
BENCHMARK(b_vec_add_eigen);
BENCHMARK(b_mat_vec_transform);
BENCHMARK(b_mat_vec_transform_eigen);
BENCHMARK(b_mat_vec_transform_array)
        ->RangeMultiplier(2)->Range(1, benchmark_size);
BENCHMARK(b_mat_vec_transform_array_eigen)
        ->RangeMultiplier(2)->Range(1, benchmark_size);

BENCHMARK_MAIN();
