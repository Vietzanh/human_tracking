from ultralytics import YOLO

model = YOLO(MODEL_PATH)
print(model.model.args)