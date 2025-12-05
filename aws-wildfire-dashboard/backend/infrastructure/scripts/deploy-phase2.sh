#!/bin/bash
set -e

echo "🚀 Deploying AWS Wildfire Detection System - Phase 2: ML Processing"
echo "===================================================================="
echo ""

# Variables
REGION="us-east-2"
STACK_PREFIX="wildfire-sensor"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CF_DIR="${SCRIPT_DIR}/../cloudformation"

echo "Configuration:"
echo "  Region: $REGION"
echo "  Stack Prefix: $STACK_PREFIX"
echo ""

# Phase 2.1: Deploy DynamoDB table for alerts
echo "Step 1/4: Deploying DynamoDB table..."
aws cloudformation deploy \
  --template-file "${CF_DIR}/02-dynamodb.yaml" \
  --stack-name ${STACK_PREFIX}-dynamodb \
  --region $REGION \
  --no-fail-on-empty-changeset

if [ $? -eq 0 ]; then
    echo "✅ DynamoDB table deployed successfully"
else
    echo "❌ Failed to deploy DynamoDB"
    exit 1
fi
echo ""

# Phase 2.2: Deploy Lambda functions
echo "Step 2/4: Deploying Lambda functions..."
"${SCRIPT_DIR}/deploy-lambda.sh"

if [ $? -eq 0 ]; then
    echo "✅ Lambda functions deployed successfully"
else
    echo "❌ Failed to deploy Lambda functions"
    exit 1
fi
echo ""

# Phase 2.3: Update IoT Rule to trigger Lambda enricher
echo "Step 3/4: Creating IoT Rule for Lambda enricher..."

# Get Lambda function ARN
ENRICHER_ARN=$(aws lambda get-function --function-name data-enricher --region $REGION --query 'Configuration.FunctionArn' --output text)

# Create Lambda permission for IoT
aws lambda add-permission \
  --function-name data-enricher \
  --statement-id AllowIoTInvoke \
  --action lambda:InvokeFunction \
  --principal iot.amazonaws.com \
  --region $REGION \
  --no-cli-pager 2>/dev/null || true

# Check if rule exists and delete it
if aws iot get-topic-rule --rule-name SensorToEnricher --region $REGION &>/dev/null; then
    echo "  Deleting existing SensorToEnricher rule..."
    aws iot delete-topic-rule --rule-name SensorToEnricher --region $REGION
fi

# Create IoT Rule
RULE_TEMP=$(mktemp)
cat > "$RULE_TEMP" <<EOF
{
  "ruleName": "SensorToEnricher",
  "topicRulePayload": {
    "description": "Route sensor data to Lambda for enrichment",
    "sql": "SELECT * FROM 'thread/sensores'",
    "actions": [
      {
        "lambda": {
          "functionArn": "${ENRICHER_ARN}"
        }
      }
    ],
    "ruleDisabled": false,
    "awsIotSqlVersion": "2016-03-23"
  }
}
EOF

aws iot create-topic-rule --cli-input-json file://"$RULE_TEMP" --region $REGION
rm "$RULE_TEMP"

echo "✅ IoT Rule for Lambda enricher created"
echo ""

# Phase 2.4: Deploy EventBridge rule for ML processor
echo "Step 4/4: Deploying EventBridge rule..."
aws cloudformation deploy \
  --template-file "${CF_DIR}/06-eventbridge.yaml" \
  --stack-name ${STACK_PREFIX}-eventbridge \
  --region $REGION \
  --no-fail-on-empty-changeset

if [ $? -eq 0 ]; then
    echo "✅ EventBridge rule deployed successfully"
else
    echo "❌ Failed to deploy EventBridge"
    exit 1
fi
echo ""

echo "===================================================================="
echo "✅ Phase 2 Deployment Complete!"
echo "===================================================================="
echo ""
echo "📊 Deployment Summary:"
echo ""

# Get stack outputs
echo "DynamoDB Table:"
TABLE_NAME=$(aws cloudformation describe-stacks --stack-name ${STACK_PREFIX}-dynamodb --region $REGION --query 'Stacks[0].Outputs[?OutputKey==`TableName`].OutputValue' --output text)
echo "  Table: $TABLE_NAME"
echo ""

echo "Lambda Functions:"
aws lambda list-functions \
    --region $REGION \
    --query 'Functions[?starts_with(FunctionName, `api-`) || contains(FunctionName, `enricher`) || contains(FunctionName, `processor`)].FunctionName' \
    --output table
echo ""

echo "IoT Rules:"
aws iot list-topic-rules --region $REGION --query 'rules[?starts_with(ruleName, `Sensor`)].ruleName' --output table
echo ""

echo "EventBridge Rules:"
RULE_NAME=$(aws cloudformation describe-stacks --stack-name ${STACK_PREFIX}-eventbridge --region $REGION --query 'Stacks[0].Outputs[?OutputKey==`RuleName`].OutputValue' --output text 2>/dev/null || echo "wildfire-ml-processor-schedule")
echo "  Rule: $RULE_NAME (runs every 5 minutes)"
echo ""

echo "===================================================================="
echo "🧪 Testing Phase 2"
echo "===================================================================="
echo ""
echo "The ML processor will run automatically every 5 minutes."
echo "To manually test the ML processor:"
echo ""
echo "aws lambda invoke \\"
echo "  --function-name ml-processor \\"
echo "  --region $REGION \\"
echo "  /tmp/ml-output.json"
echo ""
echo "cat /tmp/ml-output.json"
echo ""
echo "To check for anomalies in DynamoDB:"
echo ""
echo "aws dynamodb scan \\"
echo "  --table-name $TABLE_NAME \\"
echo "  --region $REGION \\"
echo "  --max-items 10"
echo ""
echo "===================================================================="
echo "Next Steps:"
echo "  - Wait 5-10 minutes for ML processor to run and detect anomalies"
echo "  - Verify enriched data in Timestream (with heat_index, dew_point, risk_score)"
echo "  - Check DynamoDB for anomaly alerts"
echo "  - Proceed to Phase 3: API Gateway deployment"
echo "===================================================================="
