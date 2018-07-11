#ifndef ImportRegistrationTemplateWorkOrder_H
#define ImportRegistrationTemplateWorkOrder_H
/**
 * @file
 * $Revision: 1.19 $
 * $Date: 2010/03/22 19:44:53 $
 *
 *   Unless noted otherwise, the portions of Isis written by the USGS are
 *   public domain. See individual third-party library and package descriptions
 *   for intellectual property information, user agreements, and related
 *   information.
 *
 *   Although Isis has been used by the USGS, no warranty, expressed or
 *   implied, is made by the USGS as to the accuracy and functioning of such
 *   software and related material nor shall the fact of distribution
 *   constitute any such warranty, and no responsibility is assumed by the
 *   USGS in connection therewith.
 *
 *   For additional information, launch
 *   $ISISROOT/doc//documents/Disclaimers/Disclaimers.html
 *   in a browser or see the Privacy &amp; Disclaimers page on the Isis website,
 *   http://isis.astrogeology.usgs.gov, and the USGS privacy and disclaimers on
 *   http://www.usgs.gov/privacy.html.
 */
 #include "WorkOrder.h"
 #include "ProjectItem.h"
 #include "Template.h"
 #include "TemplateList.h"

 namespace Isis {
   /**
   * @brief Add registration templates to a project
   *
   * Asks the user for a registration template and copies it into the project.
   *
   * @author 2018-07-05 Summer Stapleton
   */
    class ImportRegistrationTemplateWorkOrder : public WorkOrder {
        Q_OBJECT
      public:
        ImportRegistrationTemplateWorkOrder(Project *project);
        ImportRegistrationTemplateWorkOrder(const ImportRegistrationTemplateWorkOrder &other);
        ~ImportRegistrationTemplateWorkOrder();

        virtual ImportRegistrationTemplateWorkOrder *clone() const;

        virtual bool isExecutable(ProjectItem *item);
        bool setupExecution();
        void execute();
        void undoExecution();

      private:
        ImportRegistrationTemplateWorkOrder &operator=(const ImportRegistrationTemplateWorkOrder &rhs);

        TemplateList *m_list;
    };
 }

 #endif
