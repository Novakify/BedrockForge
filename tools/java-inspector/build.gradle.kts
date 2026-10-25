plugins { application; java }
repositories { mavenCentral() }
dependencies {
    implementation("org.ow2.asm:asm:9.11.1")
    implementation("net.fabricmc:mapping-io:0.8.0")
}
application { mainClass.set("bedrockforge.InspectJar") }
java { toolchain { languageVersion.set(JavaLanguageVersion.of(21)) } }
tasks.withType<JavaCompile>().configureEach { options.encoding = "UTF-8" }
