#include <string>
#include <random>
#include <iostream>

// Defines to make the Arduino code work with g++
#define String std::string
#define randomSeed(S) srand(S)
#define random(N) rand() % N
#define concat(S) operator+=(S)

#include "../rolling_code.h"

#define endl "\n"

using std::cout;

int main(void){
    // Rolling code test
    RollingCodes codes(1234);
    RollingCodes verifyCodes(1234);

    String code = codes.nextRollingCode();
    for(int i = 0; i < 255; i++){
        code = codes.nextRollingCode();
    }


    if(verifyCodes.verifyNextCode(code)){
        cout << "First code verified!" << endl;
    } else {
        cout << "First code \e[4;31mNOT\e[0;0m verified." << endl;
    }
    

    code = codes.nextRollingCode();

    if(verifyCodes.verifyNextCode(code)){
        cout << "Second code also verified!" << endl;
    } else {
         cout << "Second code \e[4;31mNOT\e[0;0m verified! :(" << endl;
    }


    code = "asdfasdfasdfasdfasdf";
    if(verifyCodes.verifyNextCode(code)){
        cout << "Third code (which is just random garbage) \e[4;31mwas verified...\e[0;0m" << endl;
    } else {
         cout << "Third code NOT verified! This was supposed to happen!" << endl;
    }

    code = codes.nextRollingCode();
    if(verifyCodes.verifyNextCode(code)){
        cout << "Fourth code also verified!" << endl;
    } else {
         cout << "Fourth code \e[4;31mNOT\e[0;0m verified! :(" << endl;
    }
}