/* Not currently working as different hardware seems to generate different random numbers for my implementation of the 
   of the Numerical Recipes (Press et. al.) linear congruential generator as well as the Arduino library random() function.
*/

#define ROLLING_CODE_RAND_COUNT 2

class RollingCodes{
   private:
      long multiplier = 1664525;
      long increment = 1013904223;
      long mod = pow(2, 32);
      unsigned long randSeed;

      unsigned long originalStartSeed;

   public:
      void resetSeed(){
         this->randSeed = this->originalStartSeed;
      }


      // Here to allow for declaring the object first, then setting the seed later
         // as one of my Arduino's does not work when assigning the object to NULL initially
      RollingCodes(){
         Serial.flush();
         Serial.begin(9600);

         Serial.println("You MUST set the seed before using this RollingCodes object!");

         randSeed = NULL;
      }

      void setSeed(unsigned long seed){
         if(randSeed == NULL) this->randSeed = this->originalStartSeed = seed;
         else {
            if(!Serial){
               Serial.begin(9600);
               Serial.println("Hardware serial not open, opening now at baud rate 9600 to print error messages");
            }

            Serial.println("You have already set the seed for this RollingCodes object!");
         }
      }

      RollingCodes(unsigned long privateInitialSeed){
         this->randSeed = this->originalStartSeed = privateInitialSeed;
      }

      String nextRollingCode(){
         if(randSeed == NULL){
            if(!Serial){
               Serial.begin(9600);
               Serial.println("Hardware serial not open, opening now at baud rate 9600 to print error messages");
            }

            Serial.println("You have not set the seed for this RollingCodes object!");
            return "ERROR";
         }


         String code = "";
         for(int i = 0; i < ROLLING_CODE_RAND_COUNT; i++){
            randSeed = (multiplier * randSeed + increment) % mod;
            code.concat(String(randSeed));
         }

         return code;
      }

      bool verifyNextCode(String code, unsigned int checksCount = 256){
         if(randSeed == NULL){

            if(!Serial){
               Serial.begin(9600);
               Serial.println("Hardware serial not open, opening now at baud rate 9600 to print error messages");
            }

            Serial.println("You have not set the seed for this RollingCodes object!\n\tReturning NULL");
            return NULL;
         }



         unsigned long initialSeed = randSeed;
         
         int debugCounter = 1;
         for(unsigned int i = 0; i < checksCount; i++){
            String nextCode = this->nextRollingCode();
            if(code.equals(nextCode)){
               return true;
            } else if(debugCounter <= 5){
               Serial.println("Receiver's next code (checking against) (number " + String(debugCounter) + ") is: " + nextCode);
               debugCounter++;
            }
         }
         
         randSeed = initialSeed;
         return false;
      }
};
