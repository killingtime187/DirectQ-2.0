cd DirectQ
rmdir Debug /s /q
rmdir Release /s /q
attrib *.user -r -a -s -h
del *.user /q
cd ..
attrib *.suo -r -a -s -h
attrib *.ncb -r -a -s -h
del DirectQ.suo /q
del DirectQ.ncb /q
echo done
pause
