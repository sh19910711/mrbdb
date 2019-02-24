MRuby::Build.new do |conf|
  toolchain :gcc

  conf.build_dir = ENV["MRUBY_BUILD_DIR"] || raise("MRUBY_BUILD_DIR undefined!")

  conf.cc do |cc|
    cc.flags << "-fPIC"
  end

  conf.gembox :default do |gem|
    gem.cc.flags << "-fPIC"
  end

  conf.gem './'
end
