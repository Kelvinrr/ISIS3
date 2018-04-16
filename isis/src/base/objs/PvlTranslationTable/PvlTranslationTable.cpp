/**
 * @file
 * $Revision: 1.12 $
 * $Date: 2010/01/04 17:58:51 $
 *
 *   Unless noted otherwise, the portions of Isis written by the USGS are public
 *   domain. See individual third-party library and package descriptions for
 *   intellectual property information,user agreements, and related information.
 *
 *   Although Isis has been used by the USGS, no warranty, expressed or implied,
 *   is made by the USGS as to the accuracy and functioning of such software
 *   and related material nor shall the fact of distribution constitute any such
 *   warranty, and no responsibility is assumed by the USGS in connection
 *   therewith.
 *
 *   For additional information, launch
 *   $ISISROOT/doc//documents/Disclaimers/Disclaimers.html in a browser or see
 *   the Privacy &amp; Disclaimers page on the Isis website,
 *   http://isis.astrogeology.usgs.gov, and the USGS privacy and disclaimers on
 *   http://www.usgs.gov/privacy.html.
 */

#include <fstream>
#include <sstream>

#include <QDebug>

#include "IException.h"
#include "IString.h"
#include "Message.h"
#include "Pvl.h"
#include "PvlTranslationTable.h"

using namespace std;
namespace Isis {

  /**
   * Constructs and initializes a PvlTranslationTable object
   *
   * @param transFile The translation file to be used
   *
   * @throws IException::Io
   */
  PvlTranslationTable::PvlTranslationTable(FileName transFile) {
    AddTable(transFile.expanded());
  }


  /**
   * Constructs and initializes a PvlTranslationTable object
   *
   * @param istr The translation stream to be used to translate values
   */
  PvlTranslationTable::PvlTranslationTable(std::istream &istr) {
    istr >> p_trnsTbl;
  }


  /**
   * Construct an empty PvlTranslationTable
   */
  PvlTranslationTable::PvlTranslationTable() {
  }


  /**
   * Destroys the PvlTranslationTable object.
   */
  PvlTranslationTable::~PvlTranslationTable() {
  }


  /**
   * Protected accessor for pvl translation table passed into 
   * class. This method returns a reference to the translation 
   * table member. 
   *  
   * @return @b Pvl The translation table as a PVL object. 
   */
  Pvl &PvlTranslationTable::TranslationTable() {
    return p_trnsTbl;
  }


  /**
  *  Protected accessor for const pvl translation table passed
  *  into class. This method returns a @b const reference to the
  *  translation table member.
   *  
  * @return @b Pvl The translation table as a PVL object. 
   */
  const Pvl &PvlTranslationTable::TranslationTable() const {
    return p_trnsTbl;
  }


  /**
   * Adds the contents of a translation table to the searchable groups/keys
   *
   * @param transFile The name of the translation file to be added.
   *
   * @throws IException::Io
   */
  void PvlTranslationTable::AddTable(const QString &transFile) {
    p_trnsTbl.read(FileName(transFile).expanded());
  }


  /**
   * Adds the contents of a translation table to the searchable groups/keys
   * Also performs a verification, to ensure that the translation table
   * is valid
   *
   * @param transStm The stream to be added.
   */
  void PvlTranslationTable::AddTable(std::istream &transStm) {
    transStm >> p_trnsTbl;
    
    // pair< name, size > of acceptable keywords.
    // A size of -1 means non-zero size.
    vector< pair<QString, int> > validKeywordSizes = validKeywords();

    for (int i = 0; i < p_trnsTbl.groups(); i++) {
      PvlGroup currGroup = p_trnsTbl.group(i);

      if (!currGroup.hasKeyword("InputKey")) {
        QString message = "Unable to find InputKey for group ["
                         + currGroup.name() + "] in file [" +
                         p_trnsTbl.fileName() + "]";
        throw IException(IException::User, message, _FILEINFO_);
      }

      for (int j = 0; j < currGroup.keywords(); j++) {
        bool validKeyword = false;
        bool keywordSizeMismatch = false;

        const PvlKeyword &currKey = currGroup[j];

        // Test this keyword for validity
        for (int key = 0;
            !validKeyword && key < (int)validKeywordSizes.size();
            key++) {

          // If this is the right keyword (names match) then test sizes
          if (currKey.name() == validKeywordSizes[key].first) {

            // if -1 then test that size() > 0
            if (validKeywordSizes[key].second == -1) {
              if (currKey.size() > 0) {
                validKeyword = true;
              }
            }
            // otherwise should exact match
            else if (currKey.size() == validKeywordSizes[key].second) {
              validKeyword = true;
            }
            else {
              keywordSizeMismatch = true;
            }
          }

        }

        // if we had an error report it
        if (!validKeyword) {
          if (!keywordSizeMismatch) {
            QString message = "Keyword [" + currKey.name();
            message += "] is not a valid keyword.";
            message += " Error in file [" + p_trnsTbl.fileName() + "]" ;

            throw IException(IException::User, message, _FILEINFO_);
          }
          else {
            QString message = "Keyword [" + currKey.name();
            message += "] does not have the correct number of elements.";
            message += " Error in file [" + p_trnsTbl.fileName() + "]" ;

            throw IException(IException::User, message, _FILEINFO_);
          }

        }
      }
    }
  }
  
  
  /**
   * Returns a vector of valid keyword names and their sizes.  A size of -1
   * indicates that the keyword can be any size.
   * 
   * @return @b vector<pair<QString,int>> A vector of valid keyword names and their sizes.
   */
  vector< pair<QString, int> > PvlTranslationTable::validKeywords() const {

    vector< pair<QString, int> > validKeywords;
    validKeywords.push_back(pair<QString, int>("Translation",           2));
    validKeywords.push_back(pair<QString, int>("OutputName",            1));
    validKeywords.push_back(pair<QString, int>("InputGroup",           -1));
    validKeywords.push_back(pair<QString, int>("InputPosition",        -1));
    validKeywords.push_back(pair<QString, int>("OutputPosition",       -1));
    validKeywords.push_back(pair<QString, int>("Auto",                  0));
    validKeywords.push_back(pair<QString, int>("Optional",              0));
    validKeywords.push_back(pair<QString, int>("InputKey",              1));
    validKeywords.push_back(pair<QString, int>("InputDefault",         -1));
    validKeywords.push_back(pair<QString, int>("InputKeyDependencies", -1));

    return validKeywords;
  }


  /**
   * Translates a single output value from the given translation 
   * group name and input value. 
   *
   * @param translationGroupName The name of the PVL translation 
   *                             group used to identify the
   *                             input/output keywords to be
   *                             translated. Often, this is the
   *                             same as the output keyword name.
   * @param inputKeyValue The value to be translated, from the 
   *                      input keyword.
   *
   * @return QString The translated value, for the 
   *         output keyword.
   *
   * @throws IException::Programmer
   */
  QString PvlTranslationTable::Translate(const QString translationGroupName,
                                         const QString inputKeyValue) const {

    const PvlGroup &translationGroup = findTranslationGroup(translationGroupName);

    // If no input value was passed in search using the input default
    QString tmpFValue = inputKeyValue;
    if (tmpFValue.isEmpty()) {
      if (translationGroup.hasKeyword("InputDefault")) {
        tmpFValue = (QString) translationGroup["InputDefault"];
      }
      else {
        QString msg = "No value or default value to translate for ";
        msg += "translation group [";
        msg += translationGroupName;
        msg += "] in file [" + p_trnsTbl.fileName() + "]";
        throw IException(IException::Programmer, msg, _FILEINFO_);
      }
    }

    // Search the Translation keywords for a match to the input value
    Pvl::ConstPvlKeywordIterator it = translationGroup.findKeyword("Translation",
                                      translationGroup.begin(),
                                      translationGroup.end());

    while (it != translationGroup.end()) {
      const PvlKeyword &key = *it;
      // compare the value from the input file to the second value of each Translation in the trans file.
      // ignore cases for input values  
      if (QString::compare((QString) key[1], tmpFValue, Qt::CaseInsensitive) == 0) {
        return key[0];
      }
      else if ((QString) key[1] == "*") {
        if ((QString) key[0] == "*") {
          return tmpFValue;
        }
        else {
          return key[0];
        }
      }

      it = translationGroup.findKeyword("Translation", it + 1, translationGroup.end());
    }

    QString msg = "Unable to find a translation value for [" +
                 translationGroupName +  ", " + inputKeyValue + "] in file [" +
                 p_trnsTbl.fileName() + "]";

    throw IException(IException::Programmer, msg, _FILEINFO_);
  }


  /**
   * Returns the input group name from the translation table corresponding to
   * the output name argument.
   *
   * @param translationGroupName The name of the PVL translation 
   *                             group used to identify the
   *                             input/output keywords to be
   *                             translated. Often, this is the
   *                             same as the output keyword name.
   * @param inst The occurence number of the "InputGroup" keyword
   *             (first one is zero)
   *
   * @return QString The input group name
   *
   * @throws IException::Programmer
   */
  PvlKeyword PvlTranslationTable::InputGroup(const QString translationGroupName,
                                             const int inst) const {

    const PvlGroup &translationGroup = findTranslationGroup(translationGroupName);

    //bool foundLegalInputGroup = false;

    Pvl::ConstPvlKeywordIterator it = translationGroup.findKeyword("InputPosition",
                                      translationGroup.begin(),
                                      translationGroup.end());

    int currentInstance = 0;

    // If no InputPosition keyword exists, the answer is root
    if (inst == 0 && it == translationGroup.end()) {
      PvlKeyword root("InputPosition");
      root += "ROOT";
      return root;
    }

    while (it != translationGroup.end()) {
      const PvlKeyword &result = *it;

      // This check is to prevent backtracking to the old "value,value" way of
      //   doing translation file input groups for the new keyword. Flag it
      //   immediately to give a good error message.
      if (result.size() == 1 && result[0].contains(",")) {
        QString msg = "Keyword [InputPosition] cannot have a comma [,] in ";
        msg += " the value [";
        msg += result[0];
        msg += "]";
        throw IException(IException::Programmer, msg, _FILEINFO_);
      }
      else {
        //foundLegalInputGroup = true;

        if (currentInstance == inst) {
          return result;
        }

        currentInstance ++;
      }

      it = translationGroup.findKeyword("InputPosition", it + 1, translationGroup.end());
    }

    /* Error if no containers were listed
    if (!foundLegalInputGroup) {
      QString msg = "No input position found for translation [";
      msg += translationGroupName;
      msg += "] in translation file [";
      msg += p_trnsTbl.FileName();
      msg += "]";
      throw IException::Message(IException::Programmer, msg, _FILEINFO_);
    }*/

    PvlKeyword empty;
    return empty;
  }


  /**
   * Returns the input keyword name from the translation table corresponding to
   * the output name argument.
   *
   * @param translationGroupName The name of the PVL translation 
   *                             group used to identify the
   *                             input/output keywords to be
   *                             translated. Often, this is the
   *                             same as the output keyword name.
   *  
   * @return QString The input keyword name
   *
   * @throws IException::Programmer
   */
  QString PvlTranslationTable::InputKeywordName(const QString translationGroupName) const {

    const PvlGroup &translationGroup = findTranslationGroup(translationGroupName);

    if (translationGroup.hasKeyword("InputKey")) return translationGroup["InputKey"];

    return "";
  }


  /**
   * Returns the input default value from the translation table corresponding
   * to the output name argument.
   *
   * @param translationGroupName The name of the PVL translation 
   *                             group used to identify the
   *                             input/output keywords to be
   *                             translated. Often, this is the
   *                             same as the output keyword name.
   *
   * @return QString The input default value
   *
   * @throws IException::Programmer
   */
  QString PvlTranslationTable::InputDefault(const QString translationGroupName) const {

    const PvlGroup &translationGroup = findTranslationGroup(translationGroupName);

    if (translationGroup.hasKeyword("InputDefault")) return translationGroup["InputDefault"];

    return "";
  }


  /**
   * Determines whether the given group has a default input value.
   * This method returns true if the translation group contains a 
   * PvlKeyword with the name "InputDefault". Note: no value needs
   * to be assigned to this keyword. 
   *  
   * @param translationGroupName The name of the PVL translation 
   *                             group used to identify the
   *                             input/output keywords to be
   *                             translated. Often, this is the
   *                             same as the output keyword name.
   * 
   * @return bool Indicates whether the given group has an 
   *         InputDefault keyword.
   */
  bool PvlTranslationTable::hasInputDefault(const QString translationGroupName) {

    const PvlGroup &translationGroup = findTranslationGroup(translationGroupName);

    if (translationGroup.hasKeyword("InputDefault")) return true;

    return false;
  }


  /**
   * Determines whether the given group should be automatically 
   * translated. This method returns true if the translation 
   * group contains a PvlKeyword with the name "Auto". Note: 
   * no value is assigned to this keyword. 
   *  
   * @param translationGroupName The name of the PVL translation 
   *                             group used to identify the
   *                             input/output keywords to be
   *                             translated. Often, this is the
   *                             same as the output keyword name.
   * 
   * @return bool Indicates whether the given group has an Auto 
   *         keyword.
   */
  bool PvlTranslationTable::IsAuto(const QString translationGroupName) {

    const PvlGroup &translationGroup = findTranslationGroup(translationGroupName);

    if (translationGroup.hasKeyword("Auto")) return true;

    return false;
  }


  /**
   * Determines whether the translation group is optional. This 
   * method returns true if the translation group contains a 
   * PvlKeyword with the name "Optional". Note: no value is 
   * assigned to this keyword. 
   *  
   * @param translationGroupName The name of the PVL translation 
   *                             group used to identify the
   *                             input/output keywords to be
   *                             translated. Often, this is the
   *                             same as the output keyword name.
   * 
   * @return bool Indicates whether the given group has an 
   *         Optional keyword.
   */
  bool PvlTranslationTable::IsOptional(const QString translationGroupName) {

    const PvlGroup &translationGroup = findTranslationGroup(translationGroupName);

    if (translationGroup.hasKeyword("Optional")) return true;

    return false;
  }


  /**
   * Retrieves the OutputPosition PvlKeyword for the translation 
   * group with the given name. 
   *  
   * @param translationGroupName The name of the PVL translation 
   *                             group used to identify the
   *                             input/output keywords to be
   *                             translated. Often, this is the
   *                             same as the output keyword name.
   * 
   * @return PvlKeyword The OutputPosition keyword from the given 
   *         translation group.
   */
  PvlKeyword PvlTranslationTable::OutputPosition(const QString translationGroupName) {

    const PvlGroup &translationGroup = findTranslationGroup(translationGroupName);

    if (!translationGroup.hasKeyword("OutputPosition")) {
      QString msg = "Unable to find translation keyword [OutputPostion] in [" +
                   translationGroupName + "] in file [" + p_trnsTbl.fileName() + "]";
      throw IException(IException::Programmer, msg, _FILEINFO_);

    }

    return translationGroup["OutputPosition"];
  }


  /**
   * Retrieves a string containing the value of the OutputName 
   * keyword for the translation group with the given name. 
   *  
   * @param translationGroupName The name of the PVL translation 
   *                             group used to identify the
   *                             input/output keywords to be
   *                             translated. Often, this is the
   *                             same as the output keyword name.
   * 
   * @return @b QString The value of the OutputName keyword from 
   *         the given translation group.
   */
  QString PvlTranslationTable::OutputName(const QString translationGroupName) {

    const PvlGroup &translationGroup = findTranslationGroup(translationGroupName);

    if (translationGroup.hasKeyword("OutputName")) {
      return translationGroup["OutputName"];
    }

    return "";
  }

  /**
   * Searches for translation group with the given name.
   *  
   * @see PvlObject::findGroup() 
   *  
   * @param translationGroupName The name of the PVL translation 
   *                             group used to identify the
   *                             input/output keywords to be
   *                             translated. Often, this is the
   *                             same as the output keyword name.
   * 
   * @return const PvlGroup& The first PVL group with the given name.
   *  
   * @throws IException::Programmer - "Unable to find translation 
   *                   group in file."
   */
  const PvlGroup &PvlTranslationTable::findTranslationGroup(const QString translationGroupName) const {
    if (!p_trnsTbl.hasGroup(translationGroupName)) {
      QString msg = "Unable to find translation group [" + translationGroupName +
                   "] in file [" + p_trnsTbl.fileName() + "]";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }

    return p_trnsTbl.findGroup(translationGroupName);
  }
} // end namespace isis

