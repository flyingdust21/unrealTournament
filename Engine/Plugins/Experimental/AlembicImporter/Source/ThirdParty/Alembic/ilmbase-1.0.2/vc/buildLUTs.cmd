@cl /nologo /GR /EHsc ..\..\..\..\Half\toFloat\toFloat.cpp /FetoFloat.exe
@.\toFloat.exe > ..\..\..\..\Half\toFloat.h
@del .\toFloat.obj .\toFloat.exe
@cl /nologo /GR /EHsc ..\..\..\..\Half\eLut\eLut.cpp /FeeLut.exe
@.\eLut.exe > ..\..\..\..\Half\eLut.h
@del .\eLut.exe .\eLut.obj
