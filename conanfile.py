from conan import ConanFile

class PowermdgConan(ConanFile):
    name = "powermdg"
    version = "1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"
    options = {"pic": [True, False]}
    default_options = {"pic": True}

    def requirements(self):
        self.requires("simdjson/3.10.1")
        self.requires("libcurl/8.10.0")
        self.requires("zstd/1.5.6")
        self.requires("gtest/1.17.0")
        self.requires("benchmark/1.9.5")

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.pic
