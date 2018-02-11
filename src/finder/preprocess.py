import os

def makePartition(files, cl, exts, path):
    temp = []

    for f in files:
        for e in exts:
            if e in f[-5:]:
                temp.append(f)


    path = os.path.join(path, cl)
    file = open(path, 'w')
    for f in temp:
        file.write("%s\n" % f)
    file.close


def loadClasses(path):
    filePath = os.path.join(path, 'classes')
    f = open(filePath, 'r')
    file = f.readlines()
    f.close()

    classes = dict()

    for f in file:
        f = f.split(' ')[:-1]
        cl = f[0]
        exts = f[1:]

        classes.update({cl: exts})

    return classes

def preprocess(fileIndexPath, fileIndexName):
    classes = loadClasses(fileIndexPath)

    indexFile = os.path.join(fileIndexPath, fileIndexName)
    file = open(indexFile, 'r')
    files = file.readlines()
    file.close()

    for cl in classes.keys():
        exts = classes.get(cl)

        makePartition(files, cl, exts, fileIndexPath)
