// Nightly: host unit tests, Yocto image, flash the bench, full HIL run.
pipeline {
    agent none
    triggers { cron('H 1 * * *') }
    parameters {
        string(name: 'TESTBED', defaultValue: 'tests/testbeds/lab1.json', description: 'bench description')
        string(name: 'STRESS_ITERATIONS', defaultValue: '100')
        booleanParam(name: 'SKIP_YOCTO', defaultValue: false)
    }
    options { timestamps(); timeout(time: 8, unit: 'HOURS'); buildDiscarder(logRotator(numToKeepStr: '30')) }

    stages {
        stage('Host checks') {
            agent { label 'linux' }
            steps {
                sh '''
                    cmake -S . -B build -DCMAKE_C_FLAGS="-Werror -fsanitize=address,undefined"
                    cmake --build build -j"$(nproc)"
                    ctest --test-dir build --output-on-failure --output-junit ctest.xml
                    python3 -m pytest tests/unit tests/hil --testbed sim --junitxml=sim.xml
                '''
            }
            post { always { junit 'build/ctest.xml, sim.xml' } }
        }

        stage('Yocto build') {
            when { expression { !params.SKIP_YOCTO } }
            agent { label 'yocto' }
            steps {
                sh '''
                    . ./poky/oe-init-build-env build-tcu
                    bitbake tcu-image-dev
                '''
                stash name: 'image', includes: 'build-tcu/tmp/deploy/images/**/tcu-image-dev-*.wic.zst'
            }
        }

        stage('Flash bench') {
            when { expression { !params.SKIP_YOCTO } }
            agent { label 'hil-lab1' }
            steps {
                unstash 'image'
                // Bench host drives the USB serial-download mode and power relay.
                sh 'scripts/flash_bench.sh build-tcu/tmp/deploy/images/*/tcu-image-dev-*.wic.zst'
            }
        }

        stage('HIL') {
            agent { label 'hil-lab1' }
            steps {
                sh """
                    python3 -m pytest tests/hil --testbed ${params.TESTBED} \
                        --stress-iterations ${params.STRESS_ITERATIONS} \
                        --junitxml=hil.xml -o junit_logging=all
                """
            }
            post {
                always { junit 'hil.xml' }
            }
        }
    }
}
