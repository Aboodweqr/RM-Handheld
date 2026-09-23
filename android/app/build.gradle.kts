plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.aboodweqr.rmhandheld"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.aboodweqr.rmhandheld"
        minSdk = 29
        targetSdk = 35
        versionCode = 5
        versionName = "0.1.5"
    }

    buildFeatures {
        buildConfig = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}
