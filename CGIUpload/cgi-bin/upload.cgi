#! /usr/bin/env python3
#coding:utf8
import cgi
import os
import cgitb

cgitb.enable()
form = cgi.FieldStorage()

fileitem = form['uploadfile']

if fileitem.filename:
    fn = os.path.basename(fileitem.filename)
    open('../files/' + fn, 'wb').write(fileitem.file.read())
    message = '文件 "' + fn + '" 上传成功'
else:
    message = '没有文件需要上传'


print('Content-type:text/html \n\n')
print('<meta charset="utf-8">') # 防止中文显示乱码
print(' %s' % message)
print('%s' % form.getvalue('url'))