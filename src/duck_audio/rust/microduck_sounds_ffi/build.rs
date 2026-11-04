//! 给 cdylib 写入 SONAME；没有它时 CMake 会把导入库的构建路径写进 DT_NEEDED。
fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("linux") {
        println!("cargo:rustc-cdylib-link-arg=-Wl,-soname,libmicroduck_sounds.so");
    }
}
