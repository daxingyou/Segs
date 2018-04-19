/*
 * SEGS - Super Entity Game Server
 * http://www.segs.io/
 * Copyright (c) 2006 - 2018 SEGS Team (see Authors.txt)
 * This software is licensed! (See License.txt for details)
 */

/*!
 * @addtogroup SEGSAdmin Projects/CoX/Utilities/SEGSAdmin
 * @{
 */

#include "AddNewUserDialog.h"
#include "ui_AddNewUserDialog.h"
#include "SEGSAdminTool.h"

AddNewUserDialog::AddNewUserDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AddNewUserDialog)
{
    ui->setupUi(this);
    connect(ui->buttonBox,&QDialogButtonBox::accepted,this,&AddNewUserDialog::capture_input);

}
AddNewUserDialog::~AddNewUserDialog()
{
    delete ui;
}

void AddNewUserDialog::on_add_user()
{
   ui->usernameedit->clear();
   ui->passedit->clear();
   ui->accleveledit->clear();
   show();
}

void AddNewUserDialog::capture_input()
{
QString username = ui->usernameedit->text();
QString password = ui->passedit->text();
QString acclevel = ui->accleveledit->text();
emit sendInput(username,password,acclevel);
}

//!@}
